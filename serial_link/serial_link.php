<?php
/**
 * ---------------------------------------------------------------
 * Receiver
 * ---------------------------------------------------------------
 * 
 * The receiver (PHP on a Raspberry Pi with RFM12Pi) parses the incoming data from the transmitter and logs it.
 * In particular, it'll submit the power value to an EmonCMS-API.
 *
 * Copyright (c) 2013 Simon Stamm <simon@stammtec.de>
 */

define('DEBUG', true);

$emoncms_url    = 'http://emoncms.org'; // without last /
$emoncms_apikey = 'YOUR_API_KEY';

function post2emoncms($arr, $node, $emoncms_url, $emoncms_apikey)
{
	// Remove unnecessary values for EmonCMS
	unset($arr['node_id_dst']);
	unset($arr['node_id_src']);

	// Generate URL
	$url = $emoncms_url . '/input/post.json?json=' . json_encode($arr) . '&node=' . $node . '&apikey=' . $emoncms_apikey;
	if (DEBUG) echo "[+][" . date("Y-m-d H:i:s", time()) . "] Post power to EmonCMS (" . $url . ")\n";

	// Finally call the API!
	$emoncms_content = file_get_contents($url);

	if (trim($emoncms_content) != 'ok') {
		echo "[!][" . date("Y-m-d H:i:s", time()) . "] Warning: Failed calling EmonCMS-API!\n";
	}
}

// Create stream context for serial
$c = stream_context_create(
	array('dio' =>
		array(
			'data_rate'    => 9600,
			'data_bits'    => 8,
			'stop_bits'    => 1,
			'parity'       => 0,
			'flow_control' => 0,
			'is_canonical' => 1
		)
	)
);

// Are we POSIX or Windows?  POSIX platforms do not have a
// Standard port naming scheme so it could be /dev/ttyUSB0
// or some long /dev/tty.serial_port_name_thingy on OSX.
if (PATH_SEPARATOR != ";") {
	$filename = "dio.serial:///dev/ttyAMA0";
} else {
	$filename = "dio.serial://dev/ttyAMA0";
}

// Open the stream for read and write and use it.
$f = fopen($filename, "r+", false, $c);
stream_set_timeout($f, 0,1000);

if ($f) {

	echo "[+][" . date("Y-m-d H:i:s", time()) . "] Serial Link started.\n";

	// Load last packets, so we don't double-post to the API if we closed the serial link
	if (file_exists('last_packets')) {
		$last_packets = json_decode(file_get_contents('last_packets'), true);
		echo "[+][" . date("Y-m-d H:i:s", time()) . "] Last packets were loaded.\n";
	}

	// Endless loop
	while(true) {
		// Reading packet
		$packet  = '';
		$data = fgets($f);

		if ($data && $data != "\n") {
			$packet = trim($data);
			$values = explode(" ", $packet);
			$bin_data = "";

			foreach ($values as $value) {
				$bin_data .= str_pad(dechex($value), 2, '0', STR_PAD_LEFT);
			}

			$bin_str = pack("H*" , $bin_data);

			if (isset($values[1]) && $values[1]==5) {
				$array = @unpack("Cnode_id_dst/Cnode_id_src/vpower/vcount/vtx_count", $bin_str);

				// There's something missing in the received packet
				if (!$array) {
					echo "[!][" . date("Y-m-d H:i:s", time()) . "] Warning: Packet incomplete.\n";
				} else {
					// If at least one packet was received
					if (isset($last_packets[$array['node_id_src']]['tx_count'])) {
						$tx_count = $last_packets[$array['node_id_src']]['tx_count'];

						if (($tx_count + 1) > $array['tx_count']) {
							// Our packet-counter is higher than from the JeeNode -> JeeNode must have restarted!
							if (DEBUG) echo "[+][" . date("Y-m-d H:i:s", time()) . "] JeeNode rebooted, resetting last packets.\n";
							unset($last_packets[$array['node_id_src']]);
						} elseif (($tx_count + 1) != $array['tx_count']) {
							// Our packet-counter does not match the one from JeeNode
							// => We missed at least one packet!
							$missed_packet_diff = $array['tx_count'] - ($tx_count + 1);
							echo "[!][" . date("Y-m-d H:i:s", time()) . "] Warning: " . $missed_packet_diff . " packet(s) was/were missed.\n";
						}
					}

					// Adjust packet-counter
					$last_packets[$array['node_id_src']]['tx_count'] = $array['tx_count'];

					// Packet should be not logged so far
					if (!isset($last_packets[$array['node_id_src']]['count']) || $last_packets[$array['node_id_src']]['count'] != $array['count']) {
						// Yeha, a new value is there!
						$last_packets[$array['node_id_src']]['count'] = $array['count'];
						if (DEBUG) echo "[+][" . date("Y-m-d H:i:s", time()) . "] New power value #" . $array['count'] . " (" . $array['power'] . " Watt).\n";

						// Send it over to EmonCMS
						post2emoncms($array, 5, $emoncms_url, $emoncms_apikey);
					} else {
						// Already logged this value
						if (DEBUG) echo "[>][" . date("Y-m-d H:i:s", time()) . "] Already logged value #" . $array['count'] . " (" . $array['power'] . " Watt).\n";
					}

					// Write last packets to file
					if (isset($last_packets)) {
						file_put_contents('last_packets', json_encode($last_packets));
					}
				}
			} // end node check
		} // end packet check

	} // end of while(true)

}
?>
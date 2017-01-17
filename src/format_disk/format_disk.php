<?php
/**
 * @file format_disk.php
 * @brief Disk formatting back end for Elphel393 series camera
 * @copyright Copyright (C) 2017 Elphel Inc.
 * @author Mikhail Karpenko <mikhail@elphel.com>
 *
 * @par <b>License</b>:
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

$parted_script = "format_disk.py";

function get_disks_list()
{
	global $parted_script;
	
	exec($parted_script . " --list", $output, $ret_val);
	return array("ret_val" => $ret_val, "disks" => $output);
}

function table_row($index, $disk_path, $disk_size, $sys_size, $status = "")
{
	echo "<tr id='disk_row_" . $index . "'>" .
			"<td id='path_cell_" . $index . "'>" . $disk_path . "</td>" .
			"<td>" . $disk_size . "</td>" .
			"<td>" . $sys_size . "</td>" .
			"<td id='status_cell_". $index . "'>" . $status . "</td>" .
			"</tr>";
}

function table_row_err($msg)
{
	echo "<tr>" . "<td colspan='4'>" . $msg . "</td>" . "</tr>";
}

function table_body($disks)
{
	global $parted_script;
	
	$ret_val = $disks["ret_val"];
	$num = count($disks["disks"]);
	if ($ret_val == 0 && $num > 0) {
		for ($i = 0; $i < $num; $i++) {
			$data = explode(":", $disks["disks"][$i]);
			table_row($i, $data[0], $data[1], $data[2]);
		}
	} else if ($ret_val == 0 && $num == 0) {
		exec($parted_script . " --partitions", $output, $ret);
		if ($ret == 0) {
			$msg = "Disk is already partitioned: ";
			foreach ($output as $line) {
				$plist = explode(':', $line);
				foreach ($plist as $p) {
					$msg = $msg . $p . " ";
				}
			}
			$partition = substr($plist[0], 0, strpos($plist[0], '(') - 1);
			$disk = substr($partition, 0, -1);
			table_row(0, $disk, "", "", $msg);
		} else {
			$msg = "No disks suitable for partitioning";
			table_row_err($msg);
		}
	} else {
		table_row_err($disks["disks"][0]);
	}
}

function btn_class($inactive)
{
	$class = "btn btn-danger";
	if ($inactive)
		echo $class . " disabled";
	else
		echo $class;
}

/* process commands */
if (isset($_GET["cmd"]))
	$cmd = $_GET["cmd"];
else
	$cmd = "no_command";
if ($cmd == "format") {
	if (isset($_GET["disk_path"])) {
		if (isset($_GET["force"]))
			$force = ' -f ';
		else
			$force = ' ';
		$disk_path = $_GET["disk_path"];
		exec($parted_script . $force . $disk_path, $output, $ret_val);
		if ($ret_val == 0) {
			print("OK");
		} else {
			foreach ($output as $key => $val)
				if ($val == '')
					unset($output[$key]);
			print(implode(', ', $output));
		}
		exit();
	}
} else {
	// just create the page
	$disks_list = get_disks_list();
	if ($disks_list["ret_val"] != 0 || count($disks_list["disks"]) == 0)
		$no_disk = true;
	else
		$no_disk = false;
}
	
?>

<!doctype html>
<html lang="en">
<head>
	<meta charset="utf-8"/>
	<meta name="author" content="Elphel"/>
	<link rel="stylesheet" href="js/bootstrap/css/bootstrap.css">
	<script src="js/jquery-2.2.3.js"></script>
	<script src="js/jquery-ui/jquery-ui.js"></script>
	<script src="js/bootstrap/js/bootstrap.js"></script>
	<script src="format_disk.js"></script>	
</head>

<body onload="init_actions()" style="padding-top:0px;">
	<h2 id="title" style="padding-left:10px;">Format disk</h2>
	<div style="padding-left:10px;">
		<table class="table">
			<thead>
				<tr><th>Disk</th><th>Total size</th><th>System partition</th><th>Status</th></tr>
			</thead>
			<tbody id="disks_list">
				<?php table_body($disks_list);	?>
			</tbody>
		</table>
	</div>
	<div style="padding-left:10px;">
		<span class="checkbox"><label><input id="chk_force" type="checkbox">Forse 'mkfs' to create a file system</label></span>
		<button id="btn_format" type="button" class="<?php btn_class($no_disk); ?>"><b>Format</b></button>
	</div>
</body>

</html>
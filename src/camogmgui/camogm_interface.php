<?
/*!***************************************************************************
*! FILE NAME  : camogmgui.php
*! DESCRIPTION: command interface for the camogm recorder and camogm GUI
*! Copyright (C) 2008 Elphel, Inc
*! -----------------------------------------------------------------------------**
*!
*!  This program is free software: you can redistribute it and/or modify
*!  it under the terms of the GNU General Public License as published by
*!  the Free Software Foundation, either version 3 of the License, or
*!  (at your option) any later version.
*!
*!  This program is distributed in the hope that it will be useful,
*!  but WITHOUT ANY WARRANTY; without even the implied warranty of
*!  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*!  GNU General Public License for more details.
*!
*!  You should have received a copy of the GNU General Public License
*!  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*! -----------------------------------------------------------------------------**
*!  $Log: camogm_interface.php,v $
*!  Revision 1.23  2013/05/28 01:49:19  dzhimiev
*!  1. fixed list-command
*!
*!  Revision 1.22  2013/05/26 04:41:32  dzhimiev
*!  1. minor changes to avoid php hanging when not communicating with camogm
*!
*!  Revision 1.21  2012/04/14 01:25:12  dzhimiev
*!  1. added 'list' - shows files at the specified path
*!
*!  Revision 1.20  2011/06/02 12:29:07  oneartplease
*!  added new option to set_start_after_timestamp to allow setting record start time in the future without first having to read current camera time "p5" as parameter will start recording at camera time "p"lus 5 seconds
*!
*!  Revision 1.19  2011/04/04 20:19:36  dzhimiev
*!  1. added frame skipping option
*!
*!  Revision 1.18  2011/01/01 13:04:47  oneartplease
*!  added commands to set frameskipping and secondsskipping (timelapse)
*!
*!  Revision 1.17  2010/08/20 20:16:55  oneartplease
*!  cleaned up setcookie commands
*!
*!  Revision 1.16  2010/08/08 20:19:46  dzhimiev
*!  1. added write/read elphel parameters
*!  2. camogm: debuglev , start_after_timestamp
*!
*!  Revision 1.15  2010/06/24 17:31:55  dzhimiev
*!  1. added camogm process run check (ps | grep camogm)
*!
*!  Revision 1.14  2010/01/19 10:07:20  oneartplease
*!  added unmounting command
*!
*!  Revision 1.12  2009/10/13 12:42:31  oneartplease
*!  moved save_gp=1 command
*!
*!  Revision 1.11  2009/06/24 15:20:04  oneartplease
*!  file renaming works with absolute paths now as preparation for mounting multiple devices
*!
*!  Revision 1.5  2009/03/26 12:20:40  oneartplease
*!  audio and geotagging updates, both are not fully working yet though
*!
*!  Revision 1.9  2009/01/14 08:26:19  oneartplease
*!  minor updates
*!
*!
*!  Revision 1.0  2008/09/21 Sebastian Pichelhofer
*!  start and stop commands for ajax execution
*!
*/
$cmd = $_GET['cmd'];
$debug = $_GET['debug'];
$debuglev = $_GET['debuglev'];
$cmd_pipe = "/var/state/camogm_cmd";
$cmd_state = "/var/state/camogm.state";
$cmd_port = "3456";
$default_state = "/home/root/camogm.disk";
$start_str = "camogm -n " . $cmd_pipe . " -p " . $cmd_port;

if ($cmd == "run_camogm")
{
	$camogm_running = false;
	exec('ps | grep "camogm"', $arr); 
	function low_daemon($v)
	{
		return (substr($v, -1) != ']');
	}
	
	$p = (array_filter($arr, "low_daemon"));
	$check = implode("<br />",$p);
	
	$state_file = get_state_path();
	$start_str = $start_str . " -s " . $state_file;
	
	if (strstr($check, $start_str))
		$camogm_running = true;
	else
		$camogm_running = false;
		
	if(!$camogm_running) {
		exec("rm " . $cmd_pipe);
		exec("rm " . $cmd_state);

		if (!$debug) exec($start_str.' > /dev/null 2>&1 &'); // "> /dev/null 2>&1 &" makes sure it is really really run as a background job that does not wait for input
		else         exec($start_str.$debug.' > dev/null 2>&1 &');

		for($i=0;$i<5;$i++) {
			if (file_exists($cmd_pipe))
				break;
			sleep(1);
		}

		if ($debug) {
		    $fcmd = fopen($cmd_pipe, "w");
		    fprintf($fcmd,"debuglev=$debuglev");
		    fclose($fcmd);
		}
		
		// set fast recording mode if there is at least one suitable partition or revert to legacy 'mov' mode
		$partitions = get_raw_dev();
		if (!empty($partitions)) {
			reset($partitions);
			$cmd_str = 'format=jpeg;' . 'rawdev_path=' . key($partitions) . ';';
			write_cmd_pipe($cmd_str);
		} else {
			$cmd_str = 'format=mov;save_gp=1;';
			write_cmd_pipe($cmd_str);
		}
	}
}
else if ($cmd == "status")
{
	$pipe="/var/state/camogm.state";
	$mode=0777;
	if(!file_exists($pipe)) {
		umask(0);
		posix_mkfifo($pipe,$mode);
	}
	$fcmd=fopen($cmd_pipe,"w");
	fprintf($fcmd, "xstatus=%s\n",$pipe);
	fclose($fcmd);

	$status=file_get_contents($pipe);
	header("Content-Type: text/xml");
	header("Content-Length: ".strlen($status)."\n");
	header("Pragma: no-cache\n");
	printf("%s", $status);
}
else if ($cmd == "run_status")
{	
	// is camogm running
	$camogm_running = "not running";

	exec('ps | grep "camogm"', $arr); 
	function low_daemon($v)
	{
		return (substr($v, -1) != ']');
	}
	
	$p = (array_filter($arr, "low_daemon"));
	$check = implode("<br />",$p);
	
	if (strstr($check, $start_str))
		$camogm_running = "on";
	else
		$camogm_running = "off";
	

	$status="<?xml version='1.0'?><camogm_state>\n<state>".$camogm_running."</state>\n</camogm_state>";

	header("Content-Type: text/xml");
	header("Content-Length: ".strlen($status)."\n");
	header("Pragma: no-cache\n");
	printf("%s", $status);
}
else if ($cmd=="get_hdd_space"){
	if (isset($_GET['mountpoint']))
		$mountpoint = $_GET['mountpoint'];
	else
		$mountpoint = '/mnt/sda1';
		
        if (is_dir($mountpoint)) $res = disk_free_space($mountpoint);
        else                     $res = 0;
		
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo $res;
	echo "</".$cmd.">";
	xml_footer();

}
else if ($cmd=="mount") { // mount media like HDD
	if (isset($_GET['partition']))
		$partition = $_GET['partition'];
	else 
                $partition = '/dev/sda1';
		//$partition = '/dev/hda1';

	if (isset($_GET['mountpoint']))
		$mountpoint = $_GET['mountpoint'];
	else
		$mountpoint = '/var/hdd';
	
	exec('mkdir '.$mountpoint);
	//exec('mkdir /var/hdd');
	exec('mount '.$partition." ".$mountpoint);
	//exec('mount /dev/hda1 /var/hdd');
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo "done";
	echo "</".$cmd.">";
	xml_footer();
}
else if (($cmd=="umount") || ($cmd=="unmount")) { // unmount media like HDD
	$message = "";
	if (isset($_GET['mountpoint'])) {
		$mountpoint = $_GET['mountpoint'];
		exec('umount '.$mountpoint);
		$message = "done";
	} else {
		$message = "missing argument: mountpoint";
	}

	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo $message;
	echo "</".$cmd.">";
	xml_footer();
}
else if ($cmd=="set_quality") {
	$quality = $_GET['quality'];
	$sensor_port = $_GET['sensor_port'];

	$thisFrameNumber=elphel_get_frame($sensor_port);
	elphel_set_P_value($sensor_port,ELPHEL_QUALITY,$quality+0,$thisFrameNumber+3);

	//$thisFrameNumber=elphel_get_frame();
	//elphel_wait_frame_abs($thisFrameNumber+3);
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo elphel_get_P_value($sensor_port,ELPHEL_QUALITY);
	echo "</".$cmd.">";
	xml_footer();
	
}
else if ($cmd=="get_quality") {
        $sensor_port = $_GET['sensor_port'];
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo elphel_get_P_value($sensor_port,ELPHEL_QUALITY);
	echo "</".$cmd.">";
	xml_footer();
}
else if ($cmd=="set_parameter") {
	$pname  = $_GET['pname'];
	$pvalue = $_GET['pvalue'];
	$sensor_port = $_GET['sensor_port'];

	//elphel_skip_frames($sensor_port,1);
	$thisFrameNumber=elphel_get_frame($sensor_port);

	$constant=constant("ELPHEL_$pname");
	elphel_set_P_value($sensor_port,$constant,$pvalue+0,$thisFrameNumber+3);

	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo elphel_get_P_value($sensor_port,$constant);
	echo "</".$cmd.">";
	xml_footer();
}
else if ($cmd=="get_parameter") {
	$pname  = $_GET['pname'];
	$sensor_port = $_GET['sensor_port'];
	$constant=constant("ELPHEL_$pname");
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo elphel_get_P_value($sensor_port,$constant);
	echo "</".$cmd.">";
	xml_footer();
}
else if ($cmd=="set_skip") {
	$skipping_mask = $_GET['skip_mask'];
	//!!!sub!!!
	//exec("fpcf -w 4d $skipping_mask");
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo "done";
	echo "</".$cmd.">";
	xml_footer();
}
else if ($cmd=="list") {
	if (isset($_GET['path'])) $path = $_GET['path'];
	else {
	    $message = "the path is not set";
	    break;
	}

	if (is_dir($path)) {
	    $files = scandir($path);
	    foreach ($files as $file){
		if (is_file("$path/$file")) $message .= "<f>$path/$file</f>\n";
	    }
	}else{
	    $message = "directory not found";
	    break;
	}
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	echo $message;
	echo "</".$cmd.">";
	xml_footer();
}
else if ($cmd=="list_raw_devices"){
	$devices = get_raw_dev();

	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	foreach ($devices as $device => $size) {
			echo "<item>";
			echo "<raw_device>" . $device . "</raw_device>";
			echo "<size>" . round($size / 1048576, 2) . "</size>";
			echo "</item>";
	}
	echo "</".$cmd.">";
	xml_footer();
}
else if ($cmd=="list_partitions"){
        $partitions = get_partitions();
        foreach ($partitions as $device=>$size) {
                echo "<item>";
                echo "  <device>" . $device . "</device>";
                echo "  <size>" . round($size / 1048576, 2) . "</size>";
                echo "</item>";
        }
}
else
{
	$fcmd = fopen($cmd_pipe, "w");
	
	xml_header();
	echo "<command>".$cmd."</command>";
	echo "<".$cmd.">";
	
	switch ($cmd)
	{
		case "start":
			fprintf($fcmd,"start;\n");
			break;
		case "stop":
			fprintf($fcmd,"stop;\n");
			exec('sync');
			break;	
		case "exit":
			fprintf($fcmd,"exit;\n");
			exec('sync');
			break;
		case "file_rename":
			// Now requires full path (like "/var/hdd/test1.mov") for file_old
			// and either a full path or just a filename for file_new 
			if ((!isset($_GET['file_old'])) || (!isset($_GET['file_new']))) {
				echo "wrong arguments";
				break;
			}

			$old_name = $_GET['file_old'];
			$new_name = $_GET['file_new'];
			
			if (!strrpos($new_name, "/")) {
				$new_name = substr($old_name, 0, strrpos($old_name, "/")+1).$new_name;
			}

			if ($old_name == $new_name) {
				echo "filenames match";
				break;
			}
			
			if (file_exists($new_name)) {
				echo "file_new already exists";
				break;
			}
			if (!file_exists($old_name)) {
				echo "file_old does not exist";
				break;
			}

			// no errors found, so do the rename			
			if (rename($old_name, $new_name)) 
				echo "done";
			else
				echo "undefined error";

			break;
		case "listdevices":
			exec ("cat /proc/partitions", $arr1);
			exec ("cat /proc/mounts", $arr2);
			$ret = get_mnt_dev();
			$mnt_dev = $ret["devices"];
			$j = 0;
			// first two lines are header and empty  line separator, skip them
			$i = 2;
			while($i < count($arr1)) {
				// skip flash and RAM disk partitions
				if (!strpos($arr1[$i], "mtdblock") && !strpos($arr1[$i], "ram")) {
					$temp = $arr1[$i];
					while(strstr($temp, "  ")) {
						$temp = str_replace(chr(9), " ", $temp); 
						$temp = str_replace("  ", " ", $temp);
					}
					if (preg_match_all('/ +[a-z]{3,3}[0-9]{1,1}$/', $temp, $available_partitons) > 0) {
						// remove leading spaces
						$partitions[$j] = preg_replace("/^ +/", "", $available_partitons[0][0]);
						$parts = explode(" ", $temp);
						$size[$j] = $parts[3];
						$j++;
					}
				}
				$i++;
			}
			$j = 0;
			foreach ($partitions as $partition) {
				$include = false;
				foreach ($mnt_dev as $dev) {
					if (strpos($dev, $partition))
						$include = true;
				}
				if ($include) {
					echo "<item>";
					echo "<partition>/dev/".$partition."</partition>";
					echo "<size>".round($size[$j]/1024/1024, 2) ." GB</size>";
					$j++;
					$i = 0;
					while($i < count($arr2)) {
						if(strpos($arr2[$i], $partition))
						{
							$parts = explode(" ", $arr2[$i]);
							$mountpoint = $parts[1];
							$filesystem = $parts[2];
						}
						$i++;
					}
					if ($mountpoint != "") {
						echo "<mountpoint>".$mountpoint."</mountpoint>";
						$mountpoint = "";
					} else {
						echo "<mountpoint>none</mountpoint>";
					}
					if ($filesystem != "") {
						echo "<filesystem>".$filesystem."</filesystem>";
						$filesystem = "";
					} else {
						echo "<filesystem>none</filesystem>";					
					}
					echo "</item>";
				}
			}
			break;
		case "mkdir":
			$dir_name = $_GET['name'];
			if (isset($dir_name) && (($dir_name != "") || ($dir_name != " ")))
			{
				exec('mkdir /var/hdd/'.$dir_name);
				echo "done";
				break;
			}
			else
				break;
		case "is_hdd_mounted":
			if (isset($_GET['partition']))
				$partition = $_GET['partition'];
			else 
                                $partition = '/dev/sda1';
				//$partition = '/dev/hda1';

			exec('mount', $arr);

			$mounted = implode("", $arr);
			if (strstr($mounted, $partition))
				echo substr($mounted, strpos($mounted, $partition),22);
			else
				echo "no HDD mounted";
			break;
		case "create_symlink":
			//exec('ln -s /var/hdd /mnt/flash/html/hdd');
			
			if (isset($_GET['mountpoint'])) $mountpoint = $_GET['mountpoint'];
			else                            $mountpoint = "/mnt/sda1";
			
			exec("rm /www/pages/hdd");
			exec("ln -sf $mountpoint /www/pages/hdd");
			break;
// 		case "list":
// 			if (isset($_GET['path'])) $path = $_GET['path'];
// 			else {
// 			    echo "the path is not set";
// 			    break;
// 			}
// 
// 			if (is_dir($path)) {
// 			    $files = scandir($path);
// 			    foreach ($files as $file){
// 				if (is_file("$path/$file")) echo "<f>$path/$file</f>";
// 			    }
// 			}else{
// 			    echo "directory not found";
// 			    break;
// 			}
// 
// 			break;
		case "list_files":
			if (!file_exists('/www/pages/hdd')) {
				echo "no webshare found";
			break;
			}
			$dir = $_GET["dir"];
			
			if (isset($dir) && ($dir != "") && ($dir != "/./") && ($dir != "/"))  // show "one level up" item if we are not in "home" directory
			{
				echo "<file>";
				echo "<type>updir</type>";
				echo "<name>..</name>";
				$file_remove_last_slash = substr($dir, 0, strlen($dir)-1);
				$file_pos_of_second_last_slash = strrpos($file_remove_last_slash, "/");
				$up_file = substr($dir, 0, $file_pos_of_second_last_slash+1);
				while(strpos($up_file, "//")) {
					$up_file = str_replace("//", "/", $up_file); 
				}
				if ($up_file == "")
					echo "<path>/</path>";
				else
					echo "<path>".$up_file."</path>";
				echo "<size>0</size>";
				echo "<date>0</date>";
				echo "</file>";
			}
			
			if ($handle = opendir('/www/pages/hdd/'.$dir)) {
				while ($file = readdir($handle))
				{
					if ($file != "." && $file != "..")
					{	
						echo "<file>";
						echo "<type>";
						if (is_dir("/www/pages/hdd/".$dir.$file))
							echo "dir";
						else
							echo $extension = substr($file, strrpos($file, '.')+1, strlen($file)); 
						echo "</type>";
						echo "<name>".$file."</name>";
						echo "<path>".substr($dir, 1).$file."</path>";
						$size = filesize("/www/pages/hdd/".$dir.$file);
						echo "<size>".$size."</size>";
                                                if(!ini_get('date.timezone')){
                                                    date_default_timezone_set('GMT');
                                                }
						$date = date ("d M Y H:i:s", filectime("/www/pages/hdd/".$dir.$file));
						echo "<date>".$date."</date>";
						echo "</file>";
					}
				}
				closedir($handle);
			}else{
				echo "no webshare found<br>";
                        }
			break;
		case "set_prefix":
			$prefix = $_GET['prefix'];
			fprintf($fcmd, "prefix=%s;\n", $prefix);
			setcookie("directory", $prefix);
			break;
		case "set_debuglev":
			$debuglev = $_GET['debuglev'];
			fprintf($fcmd, "debuglev=%s;\n", $debuglev);
			break;
		case "set_duration":
			$duration = $_GET['duration'];
			fprintf($fcmd, "duration=%s;\n", $duration);
			break;
		case "set_size":
			$size = $_GET['size'];
			fprintf($fcmd, "length=%s;\n", $size);
			break;
		case "set_max_frames":
			$max_frames = $_GET['max_frames'];
			fprintf($fcmd, "max_frames=%s;\n", $max_frames);
			break;
		case "set_frames_per_chunk":
			$frames_per_chunk = $_GET['frames_per_chunk'];
			fprintf($fcmd, "frames_per_chunk=%s;\n", $frames_per_chunk);
			break;
		case "set_start_after_timestamp":
			$start_after_timestamp = $_GET['start_after_timestamp'];
			// Allow setting start timestamp as relative time with "p3" = plus 3 second
			// This allows triggering recording in the future without first reading current camera time
			if (substr($start_after_timestamp, 0, 1) == "p") { 
				$start_after_timestamp = elphel_get_fpga_time() + substr($start_after_timestamp, 1) ;
				//echo "now: ".elphel_get_fpga_time()." relative: ".$start_after_timestamp; //debug
			}
			fprintf($fcmd, "start_after_timestamp=%s;\n", $start_after_timestamp);
			break;
		case "set_frameskip":
			$frameskip_value = $_GET['frameskip'];
			fprintf($fcmd, "frameskip=%s;\n", $frameskip_value);
			break;
		case "set_timelapse":
			$timelapse_value = $_GET['timelapse'];
			fprintf($fcmd, "timelapse=%s;\n", $timelapse_value);
			break;
		case "init_compressor":
			$sensor_port = $_GET['sensor_port'];
			elphel_compressor_run($sensor_port);
			break;
		case "check_audio_hardware":
			exec('arecord -l', $arr1);
			$audio_hardware = implode("", $arr1);
			if ($audio_hardware == "")
				echo "no Audio Hardware detected";
			else
			{	
				$message = substr($audio_hardware, strpos($audio_hardware, "],")+2);
				$message = substr($message, 1, strpos($message, "[")-2); 
				echo $message;
			}
			break;
		case "test_audio_playback":
			$soundfile = $_GET['soundfile'];
			exec('aplay '.$soundfile);
			break;
		case "setmov":
			exec('echo "format=mov;" > /var/state/camogm_cmd'); // Set quicktime *.mov as default format after startup.
			exec('echo "save_gp=1;\n" > /var/state/camogm_cmd'); // enable calculation of free/used buffer space
			break;
		case "setjpeg":
			exec('echo "format=jpg;" > /var/state/camogm_cmd'); // Set quicktime *.mov as default format after startup.
			exec('echo "save_gp=1;\n" > /var/state/camogm_cmd'); // enable calculation of free/used buffer space
			break;
                case "setrawdevpath":
                        $rawdev_path = $_GET['path'];
                        exec('echo "rawdev_path='.$rawdev_path.';" > /var/state/camogm_cmd');
                        break;
		case "gettime":
			echo elphel_get_fpga_time();
			break;
	}
	if ($fcmd)
		fclose($fcmd);
	echo "</".$cmd.">";
	xml_footer();
}

function xml_header() {
	header("Content-type: text/xml"); 
	header("Pragma: no-cache\n");
	echo "<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>\n";
	echo "<camogm_interface>\n";
}
function xml_footer() {
	echo "</camogm_interface>\n";
}

/** Get a list of suitable partitions. The list will contain SATA devices only and
 * will have the following format: "name" => "size_in_blocks".
 */
function get_partitions()
{
	$names = array();
	$regexp = '/([0-9]+) +(sd[a-z0-9]+$)/';
	exec("cat /proc/partitions", $partitions);

	// the first two elements of an array are table header and empty line delimiter, skip them
	for ($i = 2; $i < count($partitions); $i++) {
		// select SATA devices only
		if (preg_match($regexp, $partitions[$i], $name) == 1) {
			$names[$name[2]] = $name[1];
			$j++;
		}
	}
	return $names;
}

/** Get a list of disk devices which have file system and can be mounted. This function
 *  uses 'blkid' command from busybox.
 */ 
function get_mnt_dev()
{
	$partitions = get_partitions();
	$devices = array();
	$fs_types = array();
	foreach ($partitions as $partition => $size) {
		$res = array();
		$dev = "/dev/" . $partition;
		exec("blkid " . $dev, $res);
		if (!empty($res)) {
			$devices[$i] = preg_replace('/: +.*/', "", $res[0]);
			if (preg_match('/(?<=TYPE=")[a-z0-9]+(?=")/', $res[0], $fs) == 1)
				$fs_types[$i] = $fs[0];
			else
				$fs_types[$i] = "none";
			$i++;
		}
	}
	
	return array("devices" => $devices, "types" => $fs_types);
}

/** Get a list of devices whithout file system which can be used for raw disk storage from camogm. */
function get_raw_dev()
{
	$j = 0;
	$ret = get_mnt_dev();
	$devices = $ret["devices"]; 
	$types = $ret["types"];

	$names = get_partitions();
	
	// filter out partitions with file system 
	$i = 0;
	$raw_devices = array();

	foreach ($names as $name => $size) {
		$found = false;
		foreach ($devices as $device) {
			if (strpos($device, $name) !== false)
				$found = true;
		}
		if ($found === false) {
			// current partition is not found in the blkid list, add it to raw devices
			$raw_devices["/dev/" . $name] = $size;
			$i++;
		}
	}
	
	//special case
	if (count($raw_devices)>1) {
            foreach($raw_devices as $k=>$v){
                if (preg_match('/sd[a-z][0-9]/',$k)==0) {
                    unset($raw_devices[$k]);
                }
            }
	}
	return $raw_devices;
}

/** Check if camera was booted from NAND or SD card and modify the path where camogm will save
 * disk write pointer. */
function get_state_path()
{
	global $default_state;
	$prefix = '/tmp/rootfs.ro';
	
	if (file_exists($prefix)) {
		$ret = $prefix . $default_state;
	} else {
		$ret = $default_state;
	}

	return $ret;
}

/** Write command to camogm command pipe */
function write_cmd_pipe($cmd_str)
{
	global $cmd_pipe;
	
	$fcmd = fopen($cmd_pipe, 'w');
	if ($fcmd !== false) {
		fprintf($fcmd, $cmd_str);
		fflush($fcmd);
		fclose($fcmd);
	}
}

?>

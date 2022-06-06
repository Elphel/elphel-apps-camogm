<?php
/*!***************************************************************************
*! FILE NAME  : camogmgui.php
*! DESCRIPTION: GUI for the camogm recorder
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
*!  $Log: camogmgui.php,v $
*!  Revision 1.13  2009/10/18 14:54:50  oneartplease
*!  made buffer bar toggleable
*!  and off by default
*!
*!  Revision 1.11  2009/10/10 21:32:49  elphel
*!  Added "save_gp=1" so the free/used buffer indicator  is correct
*!
*!  Revision 1.10  2009/10/02 19:21:10  oneartplease
*!  button layout overflow fixed
*!
*!  Revision 1.9  2009/06/24 13:41:33  oneartplease
*!  Split size now shows human well  readable value next to input field
*!
*!  Revision 1.4  2009/03/26 12:20:40  oneartplease
*!  audio and geotagging updates, both are not fully working yet though
*!
*!  Revision 1.8  2009/01/14 08:26:19  oneartplease
*!  minor updates
*!
*!
*!  Revision 1.""  2008/07/30 Sebastian Pichelhofer
*!  some stability improvements
*!
*!  Revision 1.1  2008/07/25 Sebastian Pichelhofer
*!  added custom commands form and color tags for better overview of current development status
*!
*!  Revision 1.0  2008/07/08 Sebastian Pichelhofer
*!  initial revision with basic interface
*!
*/
	$reload = false;
	
	$pipe = "/var/state/camogm.state";
	$cmd_pipe = "/var/state/camogm_cmd";
	$cmd_port = "3456";
	$start_str = "camogm -n " . $cmd_pipe . " -p " . $cmd_port;
	$mode = 0777;
	$sensor_ports = elphel_num_sensors();
	$default_imgsrv_port = 2323;

	/** Draw single buffer usage bar */
	function draw_buffer_bar($port, $ports_num)
	{
		echo "<div id=\"buffer_bar_" . $port . "\" class=\"buffer_bar\" " .
			"onclick=\"toggle_buffer();\" style=\"display:none;\">Buffer " . $port . ": " .
		"<span id=\"buffer_free_" . $port . "\" class=\"buffer_free\"><div id=\"buffer_free_text_" . $port . "\">free</div></span>" . 
		"<span id=\"buffer_used_". $port .	"\" class=\"buffer_used\"><div id=\"buffer_used_text_" . $port . "\">used</div></span>" .
		"</div>";
	}
	
	/** Inject a parameter into web page that may be needed in JavaScript */
	function inject_param($name, $val)
	{
		echo "<div id=\"" . $name . "\" style=\"display:none;\">";
		echo htmlspecialchars($val);
		echo "</div>";
	}
	
	// check if any compressor is in running state
	function check_compressors($states)
	{
		global $sensor_ports;
		$ret_val = 0;
		
		for ($i = 0; $i < $sensor_ports; $i++) {
			if ($states[$i] == "running")
				$ret_val = 1;
		}
		return $ret_val;
	}
	
	if(!file_exists($pipe)) {
		// create the pipe
		umask(0);
		if(!posix_mkfifo($pipe,$mode))
			echo "Error creating pipe!";
	} else {
		unlink($pipe);
		umask(0);
		if(!posix_mkfifo($pipe,$mode))
			echo "Error creating pipe!";
	}

	// is camogm running
	$camogm_running = false;
	exec('ps | grep "camogm"', $arr); 
	function low_daemon($v)
	{
		return (substr($v, -1) != ']');
	}
	
	$p = (array_filter($arr, "low_daemon"));
	$check = implode("<br />",$p);
	
	if (strstr($check, $start_str))
		$camogm_running = true;
	else
		$camogm_running = false;

	// if camogm is not running start it 
	if (!$camogm_running)
	{
		header("Refresh: 2; url=".$_SERVER['PHP_SELF']);
		echo "<html><head>";
		echo "<script language=\"JavaScript\" src=\"camogmgui.js\"></script>";
		echo "</head><body onLoad=\"reload();\">";
		echo "<h1>starting camogm</h1><br />page will reload automatically in a few moments<br /></body></html>";
		exit;
	}
	
	if ($camogm_running) {
		$fcmd = fopen($cmd_pipe, "w");
	}
	 	
	// format tab submit
	if (isset($_POST['settings_format'])){
		if (!isset($_POST['fastrec_checkbox'])) {
			switch($_POST['container']){
				case'ogm':
					$container = $_POST['container'];
					break;
				case'jpg':
					$container = $_POST['container'];
					break;
				case'mov':
					$container = $_POST['container'];
					break;
				default:
					$container = "mov";
					break;
			}
			$rawdev_path = "";
			$prefix = $_POST['prefix'];
		} else {
			// fast recording supports jpeg format only
			$container = "jpeg";
			$rawdev_path = $_POST['rawdev_path'];
			$prefix = "";			
		}
		if ($camogm_running) {
			// set format
			fprintf($fcmd, "format=%s;\n", $container);

			// set record directory
			if ($prefix != "") {
				fprintf($fcmd, "prefix=%s;\n", $prefix);
				setcookie("directory", $prefix); // save as cookie so we can get prefered record directory even after camera reboot
			}
			
			//set debug file
			$debug = $_POST['debug'];
			$debug_file = $_POST['debugfile'];
			$debug_level = $_POST['debuglevel'];
			if ($debug == "yes")
			{
				fprintf($fcmd, "debug=%s;\ndebuglev=%s;\n", $debug_file, $debug_level);
			}	
			else	
				fprintf($fcmd, "debug;\ndebuglev=1;\n");
			
			if ($rawdev_path != "")
				fprintf($fcmd, "rawdev_path=%s;\n", $rawdev_path);
			else
				fprintf($fcmd, "rawdev_path;\n");
		}
	}
	
	// advanced tab submit
	if (isset($_POST['advanced_settings'])) {
		if ($camogm_running) {	
		
			// we can only either skip frames or seconds
			if ($_POST['fps_reduce'] == "frameskip") 
				fprintf($fcmd, "frameskip=%s;\n", $_POST['frameskip']);
			else
				fprintf($fcmd, "timelapse=%s;\n", $_POST['timelapse']);
	
			fprintf($fcmd, "timescale=%s;\n", $_POST['timescale']);
			
			if ($_POST['exif'] == "yes")
				fprintf($fcmd, "exif=1;\n");
			else
				fprintf($fcmd, "exif=0;\n");
				
			if (isset($_POST['split_size']))
				fprintf($fcmd, "length=".$_POST['split_size'].";\n");
		
			if (isset($_POST['split_length']))
				fprintf($fcmd, "duration=".$_POST['split_length'].";\n");
		}	
	}
	
	// Audio tab submit
	if (isset($_POST['settings_sound'])) {
		if ($camogm_running) {	
		if ($_POST['audio_check'] == "on") 
			fprintf($fcmd, "audio=on;\n");
		else
			fprintf($fcmd, "audio=off;\n");
			
		fprintf($fcmd, "audio_format=%s/%s;\n", $_POST['audio_sample_rate'], $_POST['audio_channels']);

		// TODO: set volume here!
				
		if ($_POST['audio_syncmode'] == "normal")
			fprintf($fcmd, "allow_sync=disable;\n");
		else
			fprintf($fcmd, "allow_sync=enable;\n");
		}
	}
	
	// Geo-Tagging tab submit
	if (isset($_POST['settings_geotagging'])) {
		if ($camogm_running) {	
			if ($_POST['geotag_enable'] == "on") 
				fprintf($fcmd, "kml=1;\n");
			else
				fprintf($fcmd, "kml=0;\n");
				
			fprintf($fcmd, "kml_period=".$_POST['geotag_period'].";\n");
			fprintf($fcmd, "kml_hhf=".$_POST['geotag_hFov']/2 .";\n");
			fprintf($fcmd, "kml_vhf=".$_POST['geotag_vFov']/2 .";\n");
			
			if($_POST['geotag_altmode'] == "absolute")
				fprintf($fcmd, "kml_alt=gps;\n");
			else
				fprintf($fcmd, "kml_alt=ground;\n");
		}
	}
	
	// Get XML data for init
	if ($camogm_running) {
		fprintf($fcmd, "xstatus=%s;\n", $pipe);
		fclose($fcmd);
		$xml_status = file_get_contents($pipe);

		require_once('xml_simple.php');
		function parse_array($element) 
		{
		    global $logdata, $logindex;
		    foreach ($element as $header => $value) 
			{
		        if (is_array($value)) 
				{
		            parse_array($value);
		            $logindex++;
		        } else 
				{
		            $logdata[$logindex][$header] = $value;
		        }
		    }
		}
	
		$parser =& new xml_simple('UTF-8');
		$request = $parser->parse($xml_status);
	
		// catch errors
		$error_code = 0;
		if (!$request) {
		    $error_code = 1;
		    echo("XML error: ".$parser->error);
		}
	
		$logdata = array();
		$logindex = 0;
		parse_array($parser->tree);
		
		// Load data from XML
		$xml_format = substr($logdata[0]['format'], 1, strlen($logdata[0]['format'])-2);
		$xml_state = substr($logdata[0]['state'], 1, strlen($logdata[0]['state'])-2);
		$xml_directory = substr($logdata[0]['prefix'], 1, strlen($logdata[0]['prefix'])-2);
		$xml_file_length = substr($logdata[0]['file_length'], 0, strlen($logdata[0]['file_length'])); // file size in bytes
		$xml_file_duration = substr($logdata[0]['file_duration'], 0, strlen($logdata[0]['file_duration']));
		$xml_frame_number = substr($logdata[0]['frame_number'], 0, strlen($logdata[0]['frame_number']));
		$xml_res_x = substr($logdata[0]['frame_width'], 0, strlen($logdata[0]['frame_width']));
		$xml_res_y = substr($logdata[0]['frame_height'], 0, strlen($logdata[0]['frame_height']));
		$xml_rawdev_path = substr($logdata[0]['raw_device_path'], 1, strlen($logdata[0]['raw_device_path']) - 2);
		
		// Advanced Settings
		$xml_timescale = substr($logdata[0]['timescale'], 0, strlen($logdata[0]['timescale']));
		$xml_frameskip = substr($logdata[0]['frames_skip'], 0, strlen($logdata[0]['frames_skip']));
		$xml_timelapse = substr($logdata[0]['seconds_skip'], 0, strlen($logdata[0]['seconds_skip']));
		$xml_exif = substr($logdata[0]['exif'], 0, strlen($logdata[0]['exif']));
		$xml_split_size = substr($logdata[0]['max_length'], 0, strlen($logdata[0]['max_length']));
		$xml_split_length = substr($logdata[0]['max_duration'], 0, strlen($logdata[0]['max_duration']));
		
		$xml_debug_file = substr($logdata[0]['debug_output'], 1, strlen($logdata[0]['debug_output'])-2);
		$xml_debug_level = substr($logdata[0]['debug_level'], 0, strlen($logdata[0]['debug_level']));
		if (($xml_debug_file == "") || ($xml_debug_file == "null") || ($xml_debug_file == "none") || ($xml_debug_file == " /dev/null"))
			$xml_debug = false;
		else
			$xml_debug = true;
			
		// Geo-Tagging 
		$xml_geotagging_enabled = substr($logdata[0]['kml_enable'], 1, strlen($logdata[0]['kml_enable'])-2);
		$xml_geotagging_hFOV = substr($logdata[0]['kml_horHalfFov'], 1, strlen($logdata[0]['kml_horHalfFov'])-2);
		$xml_geotagging_vFOV = substr($logdata[0]['kml_vertHalfFov'], 1, strlen($logdata[0]['kml_vertHalfFov'])-2);
		$xml_geotagging_near = substr($logdata[0]['kml_near'], 1, strlen($logdata[0]['kml_near'])-2);
		$xml_geotagging_heightmode = substr($logdata[0]['kml_height_mode'], 1, strlen($logdata[0]['kml_height_mode'])-2);
		$xml_geotagging_height = substr($logdata[0]['kml_height'], 1, strlen($logdata[0]['kml_height'])-2);
		$xml_geotagging_period = substr($logdata[0]['kml_period'], 0, strlen($logdata[0]['kml_period']));
				
		// Audio Recording 
		$xml_audiorecording_enabled = substr($logdata[0]['audio_enable'], 1, strlen($logdata[0]['audio_enable'])-2);
		$xml_audio_channels = substr($logdata[0]['audio_channels'], 1, strlen($logdata[0]['audio_channels'])-2);
		$xml_audio_rate = substr($logdata[0]['audio_rate'], 1, strlen($logdata[0]['audio_rate'])-2);
		$xml_audio_volume = substr($logdata[0]['audio_volume'], 1, strlen($logdata[0]['audio_volume'])-2);
		$xml_audio_syncmode = substr($logdata[0]['allow_sync'], 1, strlen($logdata[0]['allow_sync'])-2);

		// Get per sensor port parameters
		$xml_compressor_state = array();
		for ($i = 0; $i < $sensor_ports; $i++) {
			$xml_compressor_state[$i] = substr($logdata[$i]['compressor_state'], 1, strlen($logdata[$i]['compressor_state'])-2);
		}

		if ($camogm_running) {
			if (($xml_directory == "\"\"") || ($xml_directory == "")) 
			{  
				$fcmd = fopen($cmd_pipe, "w");
				if (isset($_COOKIE['directory'])) // load directory cookie if nothing has been set
				{
					$xml_directory = $_COOKIE['directory'];
					fprintf($fcmd, "prefix=%s;\n", $xml_directory);
				}
				else
					fprintf($fcmd, "prefix=%s;\n", "/www/pages/hdd/");
			}
		}
				
		if ($xml_format == "'none'") {// use quicktime mov as default if no container format has been selected
			fprintf($fcmd, "format=mov;");
		}
		// for some reason sometimes camogm was starting with all ports disabled. Temporarily enable them all (add control)
		// pipe fcmd is closed here!
//		fprintf($fcmd, "port_enable=0;port_enable=1;port_enable=2;port_enable=3;");
		//Warning: fprintf(): 4 is not a valid stream resource in /www/pages/camogmgui.php on line 355
			
	}
	
	// GUI
	?>
	<html>
	<head>
    <title>HDD / CF Recorder</title>
	<link href="camogmgui.css" rel="stylesheet" type="text/css" media="screen">
    <link href="SpryTabbedPanels.css" rel="stylesheet" type="text/css">
	<link href="SpryCollapsiblePanel.css" rel="stylesheet" type="text/css">

	<script src="camogmgui.js" type="text/javascript"></script>    
    <script src="SpryTabbedPanels.js" type="text/javascript"></script>
	<script src="SpryCollapsiblePanel.js" type="text/javascript"></script>

	</head>
	<body onLoad="init();">
	<?php
	inject_param('sensor_ports', $sensor_ports);
	?>
    <div id="sitecoloumn">
      <div id="live-image">
        <div id="CollapsiblePanel1" class="CollapsiblePanel">
          <div class="CollapsiblePanelTab" tabindex="0">Live-Preview</div>
          <div id="CollapsiblePanel1Inhalt" class="CollapsiblePanelContent">
			<div id="live-image-header">
            	<table border="0" cellpadding="2" cellspacing="0">
                    <tr>
	                    <td width="245px"></td>
                        <td width="100px"><a href="#" onClick="update_live_image();">Update</a></td>
                        <td width="100px">Size: <a href="#" onClick="size_up_image()">+</a> | <a href="#" onClick="size_down_image()">-</a></td>
                        <td width="200px">
                        	<div class="sensor_ports">
                        	<input id="live_image_auto_update" type="checkbox" onChange="live_image_auto_update_changed();" name="live_image_auto_update"> Auto Update every: 
                        	<input id="live_image_auto_update_frequency" type="text" name="live_image_auto_update_frequency" value="5.0" size="3" onChange="validate_update_freq();"> seconds
                        	</div>
                        </td>
                        <?php
                        for ($i = 0; $i < $sensor_ports; $i++) {
                        	if ($i == 0)
								echo "<td>" . "<div class=\"sensor_ports\">" . "<input type=\"radio\" name=\"selected_sensor_port\" value=\"$i\" checked=\"true\" onchange=\"update_live_image();\">" .
								"Port " . $i . "</div>" . "</td>";
                        	else
								echo "<td>" . "<div class=\"sensor_ports\">" . "<input type=\"radio\" name=\"selected_sensor_port\" value=\"$i\" onchange=\"update_live_image();\">" .
                        	"Port " . $i . "</div>" . "</td>";
                        }
                        ?>
                    </tr>
                </table>
        	</div>
        	<img id="live-picture" style="border:solid 1px #000; margin:3px;" src="http://<? echo $_SERVER['SERVER_NAME'] . ":" . $default_imgsrv_port; ?>/bimg" width="400" height="300"><br />
		  </div>
        </div>
    </div>
       	
        <div id="header">HDD / CF Recorder</div>
        <div id="buffer_toggle"><a id="buffer_toggle_link" onclick="toggle_buffer();" href="#">
        <?php if ($sensor_ports > 1) { echo "Show buffers"; } else { echo "Show buffer";}?></a>
        </div>
        <ul>
		<?php
		for ($i = 0; $i < $sensor_ports; $i++) {
			echo "<li class=\"buffer_bars_li\">";
			draw_buffer_bar($i, $sensor_ports);
			echo "</li>";
		}
		?>
		</ul>
        <div id="help_link"><a target="_blank" href="http://wiki.elphel.com/index.php?title=Camogmgui">HELP</a></div>
        <table border="0" cellpadding="2" cellspacing="5">
        <tr><td>
        <div id="files">
            <div id="files_header">
            	Device: <span id="mounted_devices">loading ...</span><br />
            	<div id="file_browser_commands">
                <table cellpadding="0" cellspacing="0" border="0">
                <tr>
                	<td width="70"><a href="#" id="refresh_file_list" title="refresh the filebrowser" onClick='list_files(getCookie("current_dir")); get_hdd_space();'><img src="images/reload.png" style="bottom:-2px; position:relative;"> Reload</a></td>
               	  	<td width="105"><a href="#" id="mount_hdd_button" onClick="mount_hdd();"> Mount</a>
                  	<a href="#" id="create_folder" onClick="create_folder();"><img src="images/create_folder.png" title="create a new folder in the currently open directory below" style="bottom:-3px; position:relative;"> Create Folder</a></td>
               	  	<td width="125"><a href="#" id="create_webshare_button" onClick="create_webshare();"> Create Webshare</a>
                  	<a href="#" id="set_rec_dir" onClick="set_rec_dir();" title="set the currently open directory below as the target folder for recording video files"><img src="images/rec_folder.png" style="bottom:-3px; position:relative;"> Set Target Folder</a></td>
                </tr>
                </table>
            	</div>
       	  </div>
            <span id="filelist">nothing mounted.</span><br />
            <div id="files_footer">Remaining Free Space: <span id="hdd_rem">loading ...</span><span id="files_current_folder">loading...</span></div>
      	</div>
        </td>
        <td valign="top">
       <div id="record" >
            <a href="#" onClick="toggle_recording();"><span id="record_text"><img src="images/record.gif" style="position:relative; bottom:-5px;"> RECORD</span></a>       </div>
       <br /><br /><br />
        <div id="TabbedPanels1" class="TabbedPanels">
          <ul class="TabbedPanelsTabGroup">
			  <? 
			  if ((check_compressors($xml_compressor_state) != 1) || (!$camogm_running))
                echo  "<li class=\"TabbedPanelsTabAlert\" tabindex=\"0\">Status</li>";
              else
              	echo "<li class=\"TabbedPanelsTab\" tabindex=\"0\">Status</li>";
              ?>
            <li class="TabbedPanelsTab" tabindex="1">Filenames</li>
            <li class="TabbedPanelsTab" tabindex="2">Format</li>
            
              <li class="TabbedPanelsTab" tabindex="3">Audio</li>
              <li class="TabbedPanelsTab" tabindex="4">Geo-Tagging</li>
              <li class="TabbedPanelsTab" tabindex="5">Advanced</li>
          </ul>
          <div class="TabbedPanelsContentGroup">
            <div class="TabbedPanelsContent">
            <!-- Status -->
            <table class="state_table" border="0px" cellpadding="opx" cellspacing="0px">
            	<!-- Table header -->
            	<tr><td width="120px"></td>
            		<?php
					for ($i = 0; $i < $sensor_ports; $i++) {
						echo "<td>Port " . $i . "</td>";
					}
            		?>
            	</tr>
            	<!-- Sensor port status -->
            	<tr><td>Sensor</td>
            	<?php 
				for ($i = 0; $i < $sensor_ports; $i++) {
					echo "<td>";
					if (elphel_get_state($i) == 0)
					{
						echo "<span class=\"alert\">not initialized!</span>";
						echo " <a href=\"#\" onClick=\"help('sensor');\"><img src=\"images/help.png\"></a>";
					}
					else
						echo "<span class=\"green\"> working</span>";
					
					echo "</td>";
				}
            	?>
            	</tr>
            	<!-- Compressor status -->
            	<tr><td>Compressor</td>
            	<?php 
				for ($i = 0; $i < $sensor_ports; $i++) {
					echo "<td>";
					if ($xml_compressor_state[$i] != "running") {
						echo "<span class=\"alert\">not running!</span>";
						echo " <a href=\"#\" OnClick=\"start_compressor(this, $i); window.location.reload();\">start compressor</a>";	
					}
					else
						echo "<span class=\"green\"> running</span>";
					echo "</td>";
				}
            	?>
            	</tr>
            	<!-- Image resolution on each port -->
            	<tr><td>Image resolution</td>
            	<?php 
            	for ($i = 0; $i < $sensor_ports; $i++) {
            		echo "<td id=\"ajax_res\">";
					echo elphel_get_P_value($i, ELPHEL_ACTUAL_WIDTH) . " x " . elphel_get_P_value($i, ELPHEL_ACTUAL_HEIGHT);
					echo "</td>";
            	}
            	?>
            	</tr>
            	<!-- JPEG quality on each port -->
            	<tr><td>JPEG quality</td>
            	<?php 
            	for ($i = 0; $i < $sensor_ports; $i++) {
            		echo "<td id=\"ajax_qual\">";
            		if (elphel_get_P_value($i, ELPHEL_COLOR) == 15){ // TIFF
            		   echo "TIFF".elphel_get_P_value($i, ELPHEL_BITS); 
            		} else {
					   echo elphel_get_P_value($i, ELPHEL_QUALITY) . " %";
            		}
					echo "</td>";
            	}
            	?>
            	</tr>
            	<!-- Frame rate on each port -->
            	<tr><td>Framerate</td>
            	<?php 
            	for ($i = 0; $i < $sensor_ports; $i++) {
            		echo "<td id=\"ajax_fps\">";
					echo elphel_get_P_value($i, ELPHEL_FP1000S) / 1000 . " fps";
					echo "</td>";
            	}
            	?>
            	</tr>
            </table>
			<br />
			<br />
            <table class="state_table" border="0px" cellpadding="0px" cellspacing="0px">
            	<tr><td width="120px">Camogm:</td>
					<td>
					<?php
					if (!$camogm_running) {
						echo "<span class=\"alert\">NOT running!</span>";
						echo " <input name=\"camogm_start\" type=\"button\" value=\"start camogm\" >";	
					}
					else
						echo "<span class=\"green\"> running</span>";
					?>
					</td>				
            	</tr>
				<tr><td>Recording:</td><td id="ajax_state"><? echo $xml_state; ?></td></tr>
				<tr><td>Audio Recording:</td><td id="ajax_audio_recording">loading...</td></tr>				
				<tr><td>Geo-Tagging:</td><td id="ajax_geotag_enabled"><? if($xml_geotagging_enabled == "yes") echo "enabled"; else echo "disabled"; ?></td></tr>
                <tr><td>Filename:</td><td id="ajax_file_name">-</td></tr>
                <tr><td>Record Time:</td><td id="ajax_file_duration">-</td></tr>
                <tr><td>File Size:</td><td id="ajax_file_length">-</td></tr>
                <tr><td>Data Rate:</td><td id="ajax_data_rate">-</td></tr>  
                <tr><td>Data Rate:</td><td id="ajax_data_rate2">-</td></tr>           
            </table>
            </div>
            <div class="TabbedPanelsContent">
            <!-- File-Names -->
            <form method="POST" name="filenames">
                    <b>File-Names:</b><br />
                    <?
					if(isset($_COOKIE['file_name_scheme']))
						$file_naming_scheme = $_COOKIE['file_name_scheme'];
					else
						$file_naming_scheme = "default";
						
					if ($file_naming_scheme == "default")
						echo "<input type=\"radio\" style=\"top:3px; position:relative;\" name=\"file_name_scheme\" onChange=\"filenamesel_changed();\" value=\"default\" checked> Default (Unix-Timestamp)<br />";
					else
						echo "<input type=\"radio\" style=\"top:3px; position:relative;\" name=\"file_name_scheme\" onChange=\"filenamesel_changed();\" value=\"default\"> Default (Unix-Timestamp)<br />";
					
					if ($file_naming_scheme == "prompt")
						echo "<input type=\"radio\" style=\"top:3px; position:relative;\" name=\"file_name_scheme\" onChange=\"filenamesel_changed();\" value=\"prompt\" checked> <span id=\"filename_prompt_text\">Prompt for filename after recording</span><br />";
					else
						echo "<input type=\"radio\" style=\"top:3px; position:relative;\" name=\"file_name_scheme\" onChange=\"filenamesel_changed();\" value=\"prompt\"> <span id=\"filename_prompt_text\">Prompt for filename after recording</span><br />";
						
					if ($file_naming_scheme == "advanced")
						echo "<input type=\"radio\" style=\"top:3px; position:relative;\" name=\"file_name_scheme\" onChange=\"filenamesel_changed();\" value=\"advanced\" checked> Advanced Naming-Schemes<br />";
					else
						echo "<input type=\"radio\" style=\"top:3px; position:relative;\" name=\"file_name_scheme\" onChange=\"filenamesel_changed();\" value=\"advanced\"> Advanced Naming-Schemes<br />";
					?>
                    <br />
                    <? 
					if ($file_naming_scheme == "advanced")
	                    echo "<div id=\"advanced_name_panel\" style=\"display:block;\">";
					else
                    	echo "<div id=\"advanced_name_panel\" style=\"display:none;\">";
					?>
                    <table width="100%">
					<tr><td><input type="checkbox" onChange="update_name_scheme();" name="prefix_enable" <? if (isset($_COOKIE['filenames_prefix_disabled'])) if ($_COOKIE['filenames_prefix_disabled'] == "false") echo "checked"; ?>> Prefix:</td><td><input name="pre_name" onKeyUp="update_name_scheme();" onChange="update_name_scheme();" type="text" value="<? if (isset($_COOKIE['filenames_prefix'])) echo $_COOKIE['filenames_prefix']; ?>"></td></tr>
                    <tr><td><input type="checkbox" onChange="update_name_scheme();" name="scene_enable" <? if (isset($_COOKIE['filenames_scene_disabled'])) if ($_COOKIE['filenames_prefix_disabled'] == "false") echo "checked"; ?>> Scene:</td><td><input id="scene" name="scene" onKeyUp="update_name_scheme();" onChange="update_name_scheme();" type="text" value="<? if (isset($_COOKIE['filenames_scene'])) echo $_COOKIE['filenames_scene']; ?>"><input type="button" value="+" onClick="raise('scene');"><input type="button" onClick="lower('scene');" value="-"></td></tr>
                    <tr><td><input type="checkbox" onChange="update_name_scheme();" name="shot_enable" <? if (isset($_COOKIE['filenames_shot_disabled'])) if ($_COOKIE['filenames_shot_disabled'] == "false") echo "checked"; ?>> Shot:</td><td><input id="shot" name="shot" onKeyUp="update_name_scheme();" onChange="update_name_scheme();" type="text" value="<? if (isset($_COOKIE['filenames_shot'])) echo $_COOKIE['filenames_shot']; ?>"><input type="button" value="+" onClick="raise('shot');"><input type="button" onClick="lower('shot');" value="-"></td></tr>
                    <tr><td><input type="checkbox" onChange="update_name_scheme();" name="take_enable" <? if (isset($_COOKIE['filenames_take_disabled'])) if ($_COOKIE['filenames_take_disabled'] == "false") echo "checked"; ?>> Take:</td><td><input id="take" name="take" onKeyUp="update_name_scheme();" onChange="update_name_scheme();" type="text" value="<? if (isset($_COOKIE['filenames_take'])) echo $_COOKIE['filenames_take']; ?>"><input type="button" value="+" onClick="raise('take');"><input type="button" value="-" onClick="lower('take');"></td></tr>
                    <tr><td><input type="checkbox" onChange="update_name_scheme();" name="custom_enable" <? if (isset($_COOKIE['filenames_custom_disabled'])) if ($_COOKIE['filenames_custom_disabled'] == "false") echo "checked"; ?>> Custom:</td><td><input name="custom" onKeyUp="update_name_scheme();" onChange="update_name_scheme();" type="text" value="<? if (isset($_COOKIE['filenames_custom'])) echo $_COOKIE['filenames_custom']; ?>"> <input id="custom_number" name="custom_number" onKeyUp="update_name_scheme();" type="text" size="4" value="<? if (isset($_COOKIE['filenames_customnumber'])) echo $_COOKIE['filenames_customnumber']; ?>"><input type="button" value="+" onClick="raise('custom_number');"><input type="button" value="-" onClick="lower('custom_number');"></td></tr>
                    <tr><td>Result:</td><td><input readonly name="result" size="40" type="text"></td></tr>
                    </table>
                    </div>              
			  </form>
            </div>
            <div class="TabbedPanelsContent">
            <!-- Format -->
            	<form method="POST" name="format">
                    <b>Format:</b><br />
                    <?
                    if ($xml_format == "ogm")
                    	echo "<input type=\"radio\" id=\"radioOgm\" style=\"top:3px; position:relative;\" name=\"container\" value=\"ogm\" onChange=\"format_changed(this);\" checked> Ogg Media Stream<br />";
                    else
                    	echo "<input type=\"radio\" id=\"radioOgm\" style=\"top:3px; position:relative;\" name=\"container\" value=\"ogm\" onChange=\"format_changed(this);\"> Ogg Media Stream<br />";
                    if ($xml_format == "mov")
                   		echo "<input type=\"radio\" id=\"radioMov\" style=\"top:3px; position:relative;\" name=\"container\" value=\"mov\" onChange=\"format_changed(this);\" checked> Apple Quicktime Movie<br />";
                    else
                    	echo "<input type=\"radio\" id=\"radioMov\" style=\"top:3px; position:relative;\" name=\"container\" value=\"mov\" onChange=\"format_changed(this);\">  Apple Quicktime Movie<br />";
                    if ($xml_format == "jpeg")
                    	echo "<input type=\"radio\" id=\"radioJpg\" style=\"top:3px; position:relative;\" name=\"container\" value=\"jpg\" onChange=\"format_changed(this);\" checked> JPEG/Tiff Sequence<br />";
                    else
                    	echo "<input type=\"radio\" id=\"radioJpg\" style=\"top:3px; position:relative;\" name=\"container\" value=\"jpg\" onChange=\"format_changed(this);\"> JPEG/Tiff Sequence<br />";
                    if ($xml_rawdev_path != "") {
                    	$fastrec_checked = "checked";
                    } else {
                    	$fastrec_checked = "";
                    }
                    ?>
                    <input id="fast_rec" type="checkbox" style="left:1px; top:3px; position:relative;" name="fastrec_checkbox" value="checked" onChange="fast_rec_changed(this)" <?php echo $fastrec_checked; ?>> Use fast recording 
                    <a href="#" onClick="help('fast_rec');"><img src="images/help.png"></a><br />
                    <br />
                    
                    Directory: <input id="directory" type="text" onChange="DirectoryChanged();" name="prefix" value="<? echo $xml_directory; ?>"><br />
                  <br />
                  Devices:  <a href="#" onClick="scan_devices();"><img src="images/reload.png" style="bottom:-2px; position:relative;"></a><br />
                  <div id="ajax_devices">loading...</div>
                  <br />
                  <input id="debug" style="top:3px; position:relative;" name="debug" type="checkbox" value="yes" <? if ($xml_debug) echo "checked"; ?>>Debug File: <input name="debugfile" id="debugfile" type="text" value="<? echo $xml_debug_file ?>" size="15"> 
                    <select name="debuglevel">
                    	<OPTION VALUE="" disabled>debug-level
						<OPTION VALUE="1" <? if ($xml_debug_level == "1") echo "selected"; ?>>1
                        <OPTION VALUE="2" <? if ($xml_debug_level == "2") echo "selected"; ?>>2
                        <OPTION VALUE="3" <? if ($xml_debug_level == "3") echo "selected"; ?>>3
						<OPTION VALUE="4" <? if ($xml_debug_level == "4") echo "selected"; ?>>4
						<OPTION VALUE="5" <? if ($xml_debug_level == "5") echo "selected"; ?>>5
						<OPTION VALUE="6" <? if ($xml_debug_level == "6") echo "selected"; ?>>6                                                
				  </select> 
                  <a href="#" onClick="help('debug');"><img src="images/help.png"></a><br />
                    <br />
                  <input id="submit_button" name="settings_format" type="submit" value="OK">
				</form>
            </div>
            <div class="TabbedPanelsContent">
	            <!-- Sound -->
                <b>Audio Recording:</b><br />
                <div class="small">requires external USB soundcard</div>
		<p style="color:red;">not operational yet!</p>
                Detected Audio Hardware: <span id="ajax_detected_audio_hardware">loading...</span> <a href="#" onClick="check_audio_hardware();"><img src="images/reload.png" style="bottom:-2px; position:relative;"></a><br />
                <br />
                Test Audio Playback: <a href="#" onClick="test_audio_playback('/www/pages/hdd/Congas.wav');"><img src="images/play_audio.png" style="position:relative; top:3px;"></a><br />
                <br />
                <form method="POST" id="audioform">
                <table cellspacing="5px">
				<tr><td>Record Audio</td><td><input type="checkbox" value="on" name="audio_check" id="audio_check" disabled onChange="update_audio_form(document.getElementById('audioform'))" <? if ($xml_audiorecording_enabled == "yes") echo "checked"; ?>></td></tr>
                <tr><td>Sample Rate:</td><td>
                <select name="audio_sample_rate" id="audio_sample_rate" disabled>
                    <option <? if ($xml_audio_rate == "44100") echo "selected"; ?>>44100</option>
                    <option <? if ($xml_audio_rate == "22050") echo "selected"; ?>>22050</option>
                    <option <? if ($xml_audio_rate == "11025") echo "selected"; ?>>11025</option>
                </select>
                </td></tr>
                <tr><td>Channels:</td><td>
				<select  name="audio_channels" id="audio_channels" disabled>
                    <option <? if ($xml_audio_channels == 2) echo "selected"; ?>>stereo</option>
                    <option <? if ($xml_audio_channels == 1) echo "selected"; ?>>mono</option>
                </select>
                </td></tr>
                <tr><td>Volume:</td><td>
				<select  name="audio_volume" id="audio_volume" disabled>
                    <option value="100" <? if ($xml_audio_volume == 100) echo "selected"; ?>>100%</option>
                    <option value="90" <? if ($xml_audio_volume == 90) echo "selected"; ?>>90%</option>
                    <option value="80" <? if ($xml_audio_volume == 80) echo "selected"; ?>>80%</option>
                    <option value="70" <? if ($xml_audio_volume == 70) echo "selected"; ?>>70%</option>
                    <option value="60" <? if ($xml_audio_volume == 60) echo "selected"; ?>>60%</option>
                    <option value="50" <? if ($xml_audio_volume == 50) echo "selected"; ?>>50%</option>      
                    <option value="40" <? if ($xml_audio_volume == 40) echo "selected"; ?>>40%</option>    
                    <option value="30" <? if ($xml_audio_volume == 30) echo "selected"; ?>>30%</option>    
                    <option value="20" <? if ($xml_audio_volume == 20) echo "selected"; ?>>20%</option>                                         
                    <option value="10" <? if ($xml_audio_volume == 10) echo "selected"; ?>>10%</option>    
                    <option value="0" <? if ($xml_audio_volume == 0) echo "selected"; ?>>0%</option>                        
                </select> volume not working, please ignore
                </td></tr>
                <tr><td>Sync Mode:</td><td>
				<select  name="audio_syncmode" id="audio_syncmode" disabled>
                    <option value="normal" <? if ($xml_audio_syncmode == "no") echo "selected"; ?>>normal</option>
                    <option value="NFS" <? if ($xml_audio_syncmode == "yes") echo "selected"; ?>>NFS</option>                     
                </select>
                </td></tr>                
   				<tr><td colspan="2"><INPUT name="settings_sound" type="submit" value="OK"></td></tr>
                </table>
                </form>
            </div>
            <div class="TabbedPanelsContent">
	            <!-- Geo-Tagging -->
                <b>Geo-Tagging:</b><br />
                <div class="small">requires external USB GPS-receiver</div>
                <p style="color:red;">not operational yet!</p>
                Detected GPS Hardware:<br />
                <table cellpadding="0" cellspacing="5px">
                <tr><td>Latitude:</td><td id="ajax_latitude"></td></tr>
                <tr><td>Longitude:</td><td id="ajax_longitude"></td></tr>
                <tr><td>Altitude:</td><td id="ajax_altitude"></td></tr>
                <tr><td>Orientation:</td><td id="ajax_orientation"></td></tr>
                </table>
                <form method="POST">
                <table cellpadding="0" cellspacing="5px">
		        <?
                if ($xml_geotagging_enabled == "yes")
					echo "<tr><td>Enable Geo-Tagging</td><td><input type=\"checkbox\" name=\"geotag_enable\" checked></td></tr>";
				else
					echo "<tr><td>Enable Geo-Tagging</td><td><input type=\"checkbox\" name=\"geotag_enable\"></td></tr>";
					
				?>
                <tr><td>Write GPS data every</td><td><input type="text" name="geotag_period" size="3" value="<? echo $xml_geotagging_period; ?>"> seconds</td></tr>
                <tr><td>Horizontal FOV</td><td><input type="text" name="geotag_hFov" size="3" value="<? echo $xml_geotagging_hFOV*2; ?>"> &deg;</td></tr>
                <tr><td>Vertical FOV</td><td><input type="text" name="geotag_vFov" size="3" value="<? echo $xml_geotagging_vFOV*2; ?>"> &deg;</td></tr>
                <tr><td>Altitude Mode:</td><td><select name="geotag_altmode"><OPTION VALUE="absolute" <? if ($xml_geotagging_heightmode == "GPS altitude") echo "selected"; ?>>absolute</OPTION><OPTION VALUE="ground" <? if ($xml_geotagging_heightmode == "map ground level") echo "selected"; ?>>relative to ground</OPTION></select></td></tr>
                <tr><td>Height Offset</td><td><input type="text" name="geotag_heightoffset" size="4" value="<? echo $xml_geotagging_height; ?>"> m</td></tr>
                <tr><td>Near</td><td><input type="text" name="geotag_near" size="4" value="<? echo $xml_geotagging_near; ?>"></td></tr>
                <tr><td colspan="2"><input name="settings_geotagging" type="submit" value="OK"></td></tr>
                </table>
                </form>
            </div>
            <div class="TabbedPanelsContent">
            <!-- Advanced -->
            <form method="POST">
				<b>Time Settings:</b><br />
                <table cellpadding="3px" cellspacing="0px;">
                <tr><td>Timescale:</td><td><input id="timescale" type="text" name="timescale" size="6" value="<? echo $xml_timescale; ?>"></td><td><span class="small">currently only works for Quicktime *.mov - values > 1.0 make the video play back slower than recorded (slow motion), values < 1.0 make the playback faster (time lapse video).</span></td></tr>
                <tr><td><input id="fps_reduce_frameskip" style="top:3px; position:relative;" name="fps_reduce" type="radio" value="frameskip" <? if ($xml_frameskip != 0) echo "checked"; ?> onChange="update_fps_reduce(this);"> Frameskip:</td><td><input id="frameskip_id" type="text" style="top:3px; position:relative;" name="frameskip" size="4" value="<? echo $xml_frameskip; ?>" <? if($xml_frameskip == "0") echo "disabled"; ?>></td><td><span class="small">reduces framerate and skips every n frames.</span></td></tr>
                <tr><td width="100px;"><input id="fps_reduce_timelapse" name="fps_reduce" type="radio" style="top:3px; position:relative;" value="timelapse" <? if ($xml_timelapse != 0) echo "checked"; ?> onChange="update_fps_reduce(this);"> Timelapse:</td><td><input size="4" style="top:3px; position:relative;" id="timelapse_id" type="text" name="timelapse" value="<? echo $xml_timelapse; ?>" <? if($xml_timelapse == "0") echo "disabled"; ?>></td><td><span class="small">reduces framerate and waits n seconds between recording single frames.</span></td></tr>
                <tr><td></td></tr>
                <tr><td>Save EXIF data</td><td><input name="exif" type="checkbox" value="yes" <? if ($xml_exif == "\"yes\"") echo "checked";?>></td></tr>
                <tr><td>Split Size:</td><td><input name="split_size" id="split_size_input" type="text" size="10" onChange="calc_split_size();" onkeyup="calc_split_size();" onBlur="calc_split_size();" value="<? echo $xml_split_size; ?>" ></td><td>Bytes <span class="small" id="split_size_calculation">...</span></td></tr>
                <tr><td>Split Length:</td><td><input name="split_length" type="text" size="10" value="<? echo $xml_split_length; ?>"></td><td>Seconds</td></tr>
                </table>
                <INPUT name="advanced_settings" type="submit" value="OK">
                </div>  
            </form>
          </div>        
          </div>
    	</div>
        </td>
        </table>
    </div>
	<script type="text/javascript">
	<!--
	if (getCookie('live_image_panel_open') == "true")
		var CollapsiblePanel1 = new Spry.Widget.CollapsiblePanel("CollapsiblePanel1", {contentIsOpen:true});
	else
		var CollapsiblePanel1 = new Spry.Widget.CollapsiblePanel("CollapsiblePanel1", {contentIsOpen:false});
		
	var TabbedPanels1 = new Spry.Widget.TabbedPanels("TabbedPanels1");

	if (getCookie('tab') != "")
		TabbedPanels1.showPanel(parseInt(getCookie('tab')));
	//-->
	</script>
</body>

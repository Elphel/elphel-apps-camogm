<?php
/*!***************************************************************************
*! FILE NAME  : camogmstate.php
*! DESCRIPTION: Just a sample program that interacts with camogm recorder
*! Copyright (C) 2007 Elphel, Inc
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
*!  $Log: camogmstate.php,v $
*!  Revision 1.1.1.1  2008/11/27 20:04:01  elphel
*!
*!
*!  Revision 1.2  2007/11/28 07:15:38  elphel
*!  added sending arbitrary commands to camogm
*!
*!  Revision 1.1  2007/11/19 03:23:21  elphel
*!  7.1.5.5 Added support for *.mov files in camogm.
*!
*!
*/

//! just for testing, asuming camogm is running and listening to /var/state/camogm_cmd
//! create a named pipe - "/var/state/camogm.state"
   $pipe="/var/state/camogm.state";
   $cmd_pipe="/var/state/camogm_cmd";
   $mode=0777;
   if(!file_exists($pipe)) {
      // create the pipe
      umask(0);
      posix_mkfifo($pipe,$mode);
   } else {
//! make sure $pipe is empty (how?) or just unlink($pipe) if it existed?
   }
//! open command pipe to camogm
   $fcmd=fopen($cmd_pipe,"w");
//! did the caller supply any command to be send to camogm?
   $cmd=$_GET['cmd'];
   if ($cmd) $cmd=urldecode($cmd).";";
//! ask it to send status to the pipe $pipe (so this program will wait for the data to appear)
   fprintf($fcmd,$cmd."xstatus=%s\n",$pipe);
   fclose($fcmd);
//! it seems that everything worked - caller (this script) waits for the file to appear
   $status=file_get_contents($pipe);
   header("Content-Type: text/xml");
   header("Content-Length: ".strlen($status)."\n");
   header("Pragma: no-cache\n");
   printf($status);
?> 

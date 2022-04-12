#!/usr/bin/env python
# encoding: utf-8
from __future__ import print_function
from __future__ import division
from subprocess import CalledProcessError

''' 
/**
 * @file format_disk.py
 * @brief Prepare and partition new disk for fast recording. This script creates two partitions on a disk: 
 * one is formatted to ext4 and the other is left unformatted for fast recording from camogm.
 * @copyright Copyright (C) 2017 Elphel Inc.
 * @author Mikhail Karpenko <mikhail@elphel.com>
 * @deffield updated: 
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
'''

__author__ = "Elphel"
__copyright__ = "Copyright 2017 Elphel Inc."
__license__ = "GPL"
__version__ = "3.0+"
__maintainer__ = "Mikhail Karpenko"
__email__ = "mikhail@elphel.com"
__status__ = "Development"

import os
import re
import sys
import stat
import argparse
import subprocess

import time

# use this % of total disk space for system partition
SYS_PARTITION_RATIO = 5

class ErrCodes(object):
    OK = 0
    WRONG_DISK = 1
    WRONG_PATH = 2
    NOT_DISK = 3
    NO_TOOLS = 4
    NO_PERMISSIONS = 5
    PART_FAILURE = 6
    PART_MOUINTED = 7
    def __init__(self, code = OK):
        """
        Prepare strings with error code description.
        """
        # the length of this list must match the number of error code defined as clas attributes
        self.err_str = ["Operation finished successfully", 
                        "The disk specified is already partitioned",
                        "Path to disk provided on the command line is invalid",
                        "The path provided is a partition, not a disk",
                        "One of the command-line utilities required for this script is not found",
                        "This scrip requires root permissions",
                        "Partitioning finished unsuccessfully",
                        "Partition mounted, umount failed",]
        self._err_code = code
    
    def err2str(self):
        """
        Convert error code to string description.
        Return: string containing error description
        """
        if self._err_code < len(self.err_str):
            ret = self.err_str[self._err_code]
        else:
            ret = "No description for this error code"
        return ret
    
    @property
    def err_code(self):
        return self._err_code

    @err_code.setter
    def err_code(self, val):
        if val < len(self.err_str):
            self._err_code = val

def check_prerequisites():
    """
    Check all tools required for disk partitioning.
    Return: emtry string if all tools are found and the name of a missing program otherwise
    """
    ret_str = ""
    check_tools = [['parted', '-v'],
                   ['mkfs.ext4', '-V']]
    # make it silent
    with open('/dev/null', 'w') as devnull:
        for tool in check_tools:
            try:
                subprocess.check_call(tool, stdout = devnull, stderr = devnull)
            except:
                ret_str = tool[0]
    return ret_str

def find_disks(partitioned = False):
    """
    Find all attached and, by default, unpartitioned SCSI disks. If a key is specified
    then a list of all attached disks is is returned.
    @param partitioned: include partitioned disks
    Return: a list containing paths to disks
    """
    dlist = []
    try:
        partitions = subprocess.check_output(['cat', '/proc/partitions'])
        # the first two elemets of the list are table header and empty line delimiter, skip them
        for partition in partitions.splitlines()[2:]:
            dev = re.search(' +(sd[a-z]$)', partition)
            if dev:
                dev_path = '/dev/{0}'.format(dev.group(1))
                if not partitioned:
                    plist = find_partitions(dev_path)
                    if not plist:
                        dlist.append(dev_path)
                else:
                    dlist.append(dev_path)
    except:
        # something went wrong, clear list to prevent accidental data loss
        del dlist[:]
    return dlist

def find_partitions(dev_path):
    """
    Find all partitions (if any) on a disk.
    @param dev_path: path to device
    Return: a list of full paths to partitions or empty list in case no partitions found on disk
    """
    plist = []
    try:
        partitions = subprocess.check_output(['cat', '/proc/partitions'])
        search_str = '([0-9]+) +({0}[0-9]+$)'.format(dev_path.rpartition('/')[-1])
        # the first two elemets of the list are table header and empty line delimiter, skip them
        for partition in partitions.splitlines()[2:]:
            dev = re.search(search_str, partition)
            if dev:
                plist.append('/dev/{0} ({1:.1f} GB)'.format(dev.group(2), int(dev.group(1)) / 1000000))
    except:
        # something went wrong, clear list to prevent accidental data loss
        del plist[:]
    return plist

def is_partition(dev_path):
    """
    Check if the path specified corresponds to partition and not to disk.
    @param dev_path: path to device
    Return: boolean value indicating if the path provided is a partition.
    """
    # disk path should end with a character only
    disk = re.search('sd[a-z]$', dev_path)
    if disk:
        ret = False
    else:
        ret = True
    return ret
    
def get_disk_size(dev_path):
    """
    Get the size of disk specified.
    @param dev_path: path to device
    Return: disk size in GB
    """
    #TODO: test error
    label="" 
    try:
        parted_print = subprocess.check_output(['parted', '-m', dev_path, 'unit', 'GB', 'print']) #, 'mklabel', 'msdos'])
        fields = parted_print.split(':')
        sz = fields[1]
        disk_size = int(sz[:-2])
        label = fields[5] #unknown label - not an error just a message
    except:
        print ("FIXME: (with fresh disk only) add ''mklabel', 'msdos'' in case of 'Error: /dev/sda: unrecognised disk label'")
        '''
        needs a separate command (before print):
        parted -s /dev/sda mklabel msdos
        '''
        return 0
    if (label == 'unknown'):
        #print("unknown label, running parted -s /dev/sda mklabel msdos!")
        try:
            parted_print = subprocess.check_output(['parted', '-s', dev_path, 'mklabel', 'msdos'])
        except:
            print ("Failed 'parted -s ", dev_path, " mklabel msdos")
            return 0
        try:
            parted_print = subprocess.check_output(['parted', '-m', dev_path, 'unit', 'GB', 'print'])
            fields = parted_print.split(':')
            sz = fields[1]
            disk_size = int(sz[:-2])
        except:
            print ("Failed 'parted -m ", dev_path, " unit GB print'")
            return 0
    return disk_size

def partition_disk(dev_path, sys_size, disk_size, dry_run = True, force = False):
    """
    Create partitions on disk and format system partition.
    @param dev_path: path to device
    @param sys_size: the size of system partition in GB
    @param disk_size: total disk size in GB
    Return: empty string in case of success or error message indicating the result of partitioning
    """
    try:
        if not dry_run:
            # create system partition
            start = 0
            end = sys_size
            subprocess.check_output(['parted', '-s', dev_path, 'unit', 'GB',
                                     'mklabel', 'msdos',
                                     'mkpart', 'primary', str(start), str(end)], stderr = subprocess.STDOUT)
            # create raw partition
            if (sys_size < disk_size):
                start = sys_size
                end = disk_size
                subprocess.check_output(['parted', '-s', dev_path, 'unit', 'GB',
                                         'mkpart', 'primary', str(start), str(end)], stderr = subprocess.STDOUT)
            # make file system on first partition; delay to let the changes propagate to the system
            time.sleep(2)
            partition = dev_path + '1'
            if force:
                cmd_str = ['mkfs.ext4', '-FF', partition]
                # if system partition contained a file system then it will be mounted right after partitioning
                # check this situation and unmount partition
                mounted = subprocess.check_output(['mount'])
                for item in mounted.splitlines():
                    mount_point = re.search('^{0}'.format(partition), item)
                    if mount_point:
                        subprocess.check_output(['umount', partition])
            else:
                cmd_str = ['mkfs.ext4', partition]
            subprocess.check_output(cmd_str, stderr = subprocess.STDOUT)
        ret_str = ""
    except subprocess.CalledProcessError as e:
        ret_str = e.output
    except OSError as e:
        ret_str = e.strerror
    return ret_str

if __name__ == "__main__":
    ret_str = check_prerequisites()
    if ret_str != "":
        ret_code = ErrCodes(ErrCodes.NO_TOOLS)
        print("{0}: {1}".format(ret_code.err2str(), ret_str))
        sys.exit(ret_code.err_code)
    if os.geteuid() != 0:
        ret_code = ErrCodes(ErrCodes.NO_PERMISSIONS)
        print(ret_code.err2str())
        sys.exit(ret_code.err_code)

    parser = argparse.ArgumentParser(description = "Prepare and partition new disk for fast recording from camogm")
    parser.add_argument('disk_path', nargs = '?', help = "path to a disk which should be partitioned, e.g /dev/sda")
    parser.add_argument('-l', '--list',       action = 'store_true', help = "list attached disk(s) suitable for partitioning along " + 
    "with their totals sizes and possible system partition sizes separated by colon")
    parser.add_argument('-e', '--errno', nargs = 1, type = int, help = "convert error number returned by the script to error message")
    parser.add_argument('-d', '--dry_run',    action = 'store_true', help = "execute the script but do not actually create partitions")
    parser.add_argument('-f', '--force',      action = 'store_true', help = "force 'mkfs' to create a file system and re-format existing partitions")
    parser.add_argument('-p', '--partitions', action = 'store_true', help = "list partitions and their sizes separated by colon")
    parser.add_argument('-a', '--all',        action = 'store_true', help = "Format single partition (no raw)")
    parser.add_argument('-r', '--reformat',   action = 'store_true', help = "Delete existing partitions, reformat (has to be unmounted)")
    args = parser.parse_args()
    if args.all:
        SYS_PARTITION_RATIO = 100
#    print ("SYS_PARTITION_RATIO=",SYS_PARTITION_RATIO)
    
    if args.reformat:
        args.force = True # 
        """
        Unmount if needed and delete MBR (delete partitions before trying to re-format)
        """
        disk_path = ""
        if args.disk_path:
            ret_code = ErrCodes()
            if os.path.exists(args.disk_path):
                mode = os.stat(args.disk_path).st_mode
                if stat.S_ISBLK(mode):
                    disk_path = args.disk_path
        else:    
            # find existing disks, take first
            disks = find_disks(True)
            if disks:
                disk_path = disks[0]
        """        
        if (disk_path != ""):
            print (disk_path)        
        else:
            print ("disk_path empty")
        """            
        if (disk_path != ""):
            with open("/proc/mounts", "r") as file:
                content = file.read()
            if (content.find(disk_path) > 0): # <0 - not founed
#                print (disk_path+" is mounted")
                mount_point = content[content.find(disk_path):].split()[1]
                # try to unmount
                try:
                    subprocess.check_output(['umount', mount_point])
                except:
                    ret_code = ErrCodes()
                    ret_code.err_code = ErrCodes.PART_MOUINTED
                    print(ret_code.err2str())
                    sys.exit(ret_code.err_code)
            disk_name = disk_path[:8]# /dev/sdx
            #delete MBR on this device : dd if=/dev/zero of=/dev/sda bs=512 count=1
            try:
                subprocess.check_output(['dd', 'if=/dev/zero', 'of='+disk_name,'bs=512', 'count=1'])
            except:
                ret_code = ErrCodes()
                
    if args.list:
        disks = find_disks(args.force)
        for disk in disks:
            total_size = get_disk_size(disk)
            if total_size > 0:
                sys_size = total_size * (SYS_PARTITION_RATIO / 100)
            else:
                sys_size = 0
            print('{0}:{1} GB:{2} GB'.format(disk, total_size, sys_size))
    elif args.partitions:
        all_partitions = []
        dlist = find_disks(partitioned = True)
        for disk in dlist:
            all_partitions += find_partitions(disk)
        print(':'.join(all_partitions))
    elif args.errno:
        ret = ErrCodes(args.errno[0])
        print(ret.err2str())
    elif args.disk_path:
        disk_path = ""
        ret_code = ErrCodes()
        if os.path.exists(args.disk_path):
            mode = os.stat(args.disk_path).st_mode
            if stat.S_ISBLK(mode):
                if not is_partition(args.disk_path):
                    disk_path = args.disk_path
                    plist = find_partitions(disk_path)
                    if not plist:
                        # OK, disk is not partitioned and we can proceed
                        ret_code.err_code = ErrCodes.OK
                    else:
                        # stop, disk is already partitioned
                        ret_code.err_code = ErrCodes.WRONG_DISK
                else:
                    ret_code.err_code = ErrCodes.NOT_DISK
            else:
                ret_code.err_code = ErrCodes.WRONG_PATH
        else:
            ret_code.err_code = ErrCodes.WRONG_PATH
        if ret_code.err_code != ErrCodes.OK:
            print(ret_code.err2str())
            sys.exit(ret_code.err_code)
            
        total_size = get_disk_size(disk_path)
        if total_size > 0:
            sys_size = total_size * (SYS_PARTITION_RATIO / 100)
            if args.force:
                force = args.force
            else:
                force = False
            ret_str = partition_disk(disk_path, sys_size, total_size, args.dry_run, force)
            if ret_str:
                ret_code = ErrCodes(ErrCodes.PART_FAILURE)
                print('{0}: {1}'.format(ret_code.err2str(), ret_str))
                sys.exit(ret_code.err_code)
    else:
        parser.print_help()
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from struct import pack
from time import sleep

import errno
import glob
import os
import subprocess

try:
    import pytest
except ImportError:
    print("Unable to import pytest python module.")
    print("\nIf not already installed, you may do so with:")
    print("\t\tpip3 install pytest")
    exit(1)

SOCKETS = glob.glob('/sys/bus/auxiliary/devices/intel_vsec.sdsi.*')
NUM_SOCKETS = len(SOCKETS)

MODULE_NAME = 'sdsi'
DEV_PREFIX = 'intel_vsec.sdsi'
CLASS_DIR = '/sys/bus/auxiliary/devices'
GUID = "0x6dd191"

def read_bin_file(file):
    with open(file, mode='rb') as f:
        content = f.read()
    return content

def get_dev_file_path(socket, file):
    return CLASS_DIR + '/' + DEV_PREFIX + '.' + str(socket) + '/' + file

class TestSDSiDriver:
    def test_driver_loaded(self):
        lsmod_p = subprocess.Popen(('lsmod'), stdout=subprocess.PIPE)
        result = subprocess.check_output(('grep', '-q', MODULE_NAME), stdin=lsmod_p.stdout)

@pytest.mark.parametrize('socket', range(0, NUM_SOCKETS))
class TestSDSiFilesClass:

    def read_value(self, file):
        f = open(file, "r")
        value = f.read().strip("\n")
        return value

    def get_dev_folder(self, socket):
        return CLASS_DIR + '/' + DEV_PREFIX + '.' + str(socket) + '/'

    def test_sysfs_files_exist(self, socket):
        folder = self.get_dev_folder(socket)
        print (folder)
        assert os.path.isfile(folder + "guid") == True
        assert os.path.isfile(folder + "provision_akc") == True
        assert os.path.isfile(folder + "provision_cap") == True
        assert os.path.isfile(folder + "state_certificate") == True
        assert os.path.isfile(folder + "registers") == True

    def test_sysfs_file_permissions(self, socket):
        folder = self.get_dev_folder(socket)
        mode = os.stat(folder + "guid").st_mode & 0o777
        assert mode == 0o444    # Read all
        mode = os.stat(folder + "registers").st_mode & 0o777
        assert mode == 0o400    # Read owner
        mode = os.stat(folder + "provision_akc").st_mode & 0o777
        assert mode == 0o200    # Read owner
        mode = os.stat(folder + "provision_cap").st_mode & 0o777
        assert mode == 0o200    # Read owner
        mode = os.stat(folder + "state_certificate").st_mode & 0o777
        assert mode == 0o400    # Read owner

    def test_sysfs_file_ownership(self, socket):
        folder = self.get_dev_folder(socket)

        st = os.stat(folder + "guid")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "registers")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "provision_akc")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "provision_cap")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "state_certificate")
        assert st.st_uid == 0
        assert st.st_gid == 0

    def test_sysfs_file_sizes(self, socket):
        folder = self.get_dev_folder(socket)

        if self.read_value(folder + "guid") == GUID:
            st = os.stat(folder + "registers")
            assert st.st_size == 72

        st = os.stat(folder + "provision_akc")
        assert st.st_size == 1024

        st = os.stat(folder + "provision_cap")
        assert st.st_size == 1024

        st = os.stat(folder + "state_certificate")
        assert st.st_size == 4096

    def test_no_seek_allowed(self, socket):
        folder = self.get_dev_folder(socket)
        rand_file = bytes(os.urandom(8))

        f = open(folder + "state_certificate", "rb")
        f.seek(1)
        with pytest.raises(OSError) as error:
            f.read()
        assert error.value.errno == errno.ESPIPE
        f.close()

        f = open(folder + "provision_cap", "wb", 0)
        f.seek(1)
        with pytest.raises(OSError) as error:
            f.write(rand_file)
        assert error.value.errno == errno.ESPIPE
        f.close()

        f = open(folder + "provision_akc", "wb", 0)
        f.seek(1)
        with pytest.raises(OSError) as error:
            f.write(rand_file)
        assert error.value.errno == errno.ESPIPE
        f.close()

    def test_registers_seek(self, socket):
        folder = self.get_dev_folder(socket)

        # Check that the value read from an offset of the entire
        # file is none-zero and the same as the value read
        # from seeking to the same location
        f = open(folder + "registers", "rb")
        data = f.read()
        f.seek(64)
        id = f.read()
        assert id != bytes(0)
        assert data[64:] == id
        f.close()

@pytest.mark.parametrize('socket', range(0, NUM_SOCKETS))
class TestSDSiMailboxCmdsClass:
    def test_provision_akc_eoverflow_1017_bytes(self, socket):

        # The buffer for writes is 1k, of with 8 bytes must be
        # reserved for the command, leaving 1016 bytes max.
        # Check that we get an overflow error for 1017 bytes.
        node = get_dev_file_path(socket, "provision_akc")
        rand_file = bytes(os.urandom(1017))

        f = open(node, 'wb', 0)
        with pytest.raises(OSError) as error:
            f.write(rand_file)
        assert error.value.errno == errno.EOVERFLOW
        f.close()

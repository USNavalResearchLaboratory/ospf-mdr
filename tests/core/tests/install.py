# Copyright (C) 2012 The Boeing Company
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

import quagga.test
import os
import subprocess

class TestQuaggaInstall(quagga.test.TestCase):
    'check that zebra, ospfd, and ospf6d are installed'

    def run_dash_v(self, cmd):
        '''\
        run cmd with stdout suppressed
        give a single command-line argument: -v

        check that the exit status is zero
        '''
        args = (cmd, '-v')
        err = subprocess.call(args, stdout = open(os.devnull, 'w'))
        self.assertEqual(err, 0, 'nonzero exit status')

    def test_ospfd(self):
        'install: check that ospfd runs'
        self.run_dash_v('ospfd')

    def test_ospf6d(self):
        'install: check that ospf6d runs'
        self.run_dash_v('ospf6d')

    def test_zebra(self):
        'install: check that zebra runs'
        self.run_dash_v('zebra')

def suite():
    return quagga.test.makeSuite(TestQuaggaInstall)

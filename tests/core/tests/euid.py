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

class TestEuid(quagga.test.TestCase):
    'check that this is running with superuser privileges'

    def test_euid(self):
        'check that we\'re running as root'
        euid = os.geteuid()
        self.assertEqual(euid, 0, 'non-root euid: %s' % euid)

def suite():
    return quagga.test.makeSuite(TestEuid)

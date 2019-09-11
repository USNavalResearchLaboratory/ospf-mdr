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

import unittest
import sys

class TestCase(unittest.TestCase):
    enabled = True

class QuaggaTestCase(unittest.TestCase):
    'a base class for quagga test cases'

    enabled = True
    topology = None
    result = None
    debug_wait = False

    def setUp(self):
        raise NotImplementedError

    def tearDown(self):
        if self.result is not None and \
                (not self.result.wasSuccessful() or self.result.shouldStop):
            if self.debug_wait:
                self.result.printErrors()
                sys.stderr.write('Press Enter to continue...')
                sys.stdin.read(1)
        self.result = None
        if self.topology is not None:
            self.topology.shutdown()

    def run(self, result = None):
        self.result = result
        unittest.TestCase.run(self, result)

def makeSuite(*testcases):
    s = unittest.TestSuite()
    for testcase in testcases:
        if testcase.enabled:
            s.addTest(unittest.makeSuite(testcase))
    return s

#!/usr/bin/env python

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
import os
import optparse
import sys
import tests
import quagga.test

QUAGGA_PATHS = ['/usr/lib/quagga']

def setup_sys_path():
    if '/usr/lib/python2.6/site-packages' in sys.path:
        sys.path.append('/usr/local/lib/python2.6/site-packages')
    if '/usr/lib64/python2.6/site-packages' in sys.path:
        sys.path.append('/usr/local/lib64/python2.6/site-packages')
    if '/usr/lib/python2.7/site-packages' in sys.path:
        sys.path.append('/usr/local/lib/python2.7/site-packages')
    if '/usr/lib64/python2.7/site-packages' in sys.path:
        sys.path.append('/usr/local/lib64/python2.7/site-packages')

def setup_path():
    path = os.environ['PATH'].split(':')
    for p in QUAGGA_PATHS:
        if p not in path and os.path.isdir(p):
            os.environ['PATH'] += ':' + p

def setup():
    setup_sys_path()
    setup_path()

__alltests = None
def alltests():
    global __alltests
    if __alltests is None:
        def pkgtests(pkg, prefix = ''):
            testlist = []
            if hasattr(pkg, '__all__'):
                for name in map(lambda x: '%s.%s' % (pkg.__name__, x),
                                pkg.__all__):
                    exec 'import ' + name in globals(), locals()
                    module = eval(name)
                    if hasattr(module, '__path__'):
                        testlist += pkgtests(module, name)
                    else:
                        testlist.append(module)
            return testlist
        __alltests = pkgtests(tests)
    return __alltests

class SerialTestSuite(unittest.TestSuite):
    'stop running tests when a test fails'
    def run(self, result):
        for test in self._tests:
            test(result)
            if not result.wasSuccessful() or result.shouldStop:
                break
        return result

def suite(debug_wait = False):
    quagga.test.QuaggaTestCase.debug_wait = debug_wait
    s = SerialTestSuite()
    for t in alltests():
        s.addTest(t.suite())
    return s

def main():
    usagestr = 'usage: %prog [-h] [options] [args]'
    parser = optparse.OptionParser(usage = usagestr)
    parser.set_defaults(debug = False)

    parser.add_option('-d', '--debug', dest = 'debug', action = 'store_true',
                      help = 'wait to allow debugging if a test fails; '
                      'default = %s' % parser.defaults['debug'])

    def usage(msg = None, err = 0):
        sys.stdout.write('\n')
        if msg:
            sys.stdout.write(msg + '\n\n')
        parser.print_help()
        sys.exit(err)

    # parse command line options
    options, args = parser.parse_args()

    for a in args:
        sys.stderr.write('ignoring command line argument: \'%s\'\n' % a)

    setup()
    r = unittest.TextTestRunner(verbosity = 2)
    s = suite(debug_wait = options.debug)
    r.run(s)

if __name__ == '__main__':
    main()

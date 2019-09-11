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
import quagga.topology.line
import ospfv3

from core.misc import ipaddr

class TestOspfv3Line(quagga.test.QuaggaTestCase):
    '''test basic OSPFv3 routing for a line topology

    test topology:

       n0                 n1                 nn-1
      eth0    ---   eth0      eth1    ...   eth0
    '''

    numnodes = None
    network = None
    af = None

    quagga_conf_template_common = '''\
debug ospf6 neighbor state
!
'''

    quagga_conf_template_1 = quagga_conf_template_common + '''\
interface eth0
  ipv6 ospf6 network %(network)s
  ipv6 ospf6 instance-id %(instanceid)s
!
router ospf6
  router-id %(routerid)s
  interface eth0 area 0.0.0.0
'''

    quagga_conf_template_2 = quagga_conf_template_common + '''\
interface eth0
  ipv6 ospf6 network %(network)s
  ipv6 ospf6 instance-id %(instanceid)s
!
interface eth1
  ipv6 ospf6 network %(network)s
  ipv6 ospf6 instance-id %(instanceid)s
!
router ospf6
  router-id %(routerid)s
  interface eth0 area 0.0.0.0
  interface eth1 area 0.0.0.0
'''

    def setUp(self):
        assert self.numnodes is not None
        assert self.network is not None
        assert self.af is not None

        if self.af == ipaddr.AF_INET6:
            instanceid = 0
        elif self.af == ipaddr.AF_INET:
            instanceid = 64
        else:
            raise ValueError, 'invalid af: %s' % self.af

        self.topology = quagga.topology.line.Line(self.numnodes, self.network)

        for i in xrange(self.numnodes):
            d = {'network': self.network,
                 'instanceid': instanceid,
                 'routerid': '0.0.0.%s' % (i + 1),
                 }
            if 0 < i and i < self.numnodes - 1:
                quagga_conf_template = self.quagga_conf_template_2
            else:
                quagga_conf_template = self.quagga_conf_template_1
            self.topology.n[i].quagga_conf = quagga_conf_template % d

        self.topology.startup()

    def CheckOspfv3Stable(self):
        # wait to converge
        stable = ospfv3.Ospfv3StableBarrier(self.topology, stableDuration = 10)
        stable.Wait(60)

    def CheckOspfv3Neighbors(self):
        for i in xrange(self.numnodes):
            if i > 0:
                prevrid = '0.0.0.%s' % (i)
            else:
                prevrid = None

            if i < self.numnodes - 1:
                nextrid = '0.0.0.%s' % (i + 2)
            else:
                nextrid = None

            if i == 0:
                nextif = 'eth0'
            else:
                nextif = 'eth1'

            nbrs = self.topology.n[i].Ospfv3Neighbors()
            if prevrid:
                self.topology.n[i].CheckNeighbor(nbrs, prevrid, 'eth0', 'Full')
            if nextrid:
                self.topology.n[i].CheckNeighbor(nbrs, nextrid, nextif, 'Full')

    def CheckOspfv3Routes(self, prefixes):
        for n in self.topology.n:
            n.CheckOspfv3Routes(prefixes)

    def CheckZebraRoutes(self, prefixes):
        ipv6 = self.af == ipaddr.AF_INET6
        for n in self.topology.n:
            n.CheckZebraRoutes(prefixes, ipv6)

    def CheckKernelRoutes(self, prefixes):
        ipv6 = self.af == ipaddr.AF_INET6
        for n in self.topology.n:
            n.CheckKernelRoutes(prefixes, ipv6)

    def CheckPing(self):
        for n in self.topology.n:
            for m in self.topology.n:
                if m is n:
                    continue
                n.CheckPing(m, self.af)

    def test_line(self):
        'ospfv3: line test: check convergence, neighbor state, and routes'

        self.CheckOspfv3Stable()

        self.CheckOspfv3Neighbors()

        allprefixes = filter(lambda x: x.af == self.af,
                             self.topology.AllPrefixes())

        self.CheckOspfv3Routes(allprefixes)
        self.CheckZebraRoutes(allprefixes)
        self.CheckKernelRoutes(allprefixes)

        self.CheckPing()

class TestOspfv3LineIpv4(TestOspfv3Line):
    network = 'broadcast'
    numnodes = 4
    af = ipaddr.AF_INET

class TestOspfv3LineIpv6(TestOspfv3Line):
    network = 'broadcast'
    numnodes = 4
    af = ipaddr.AF_INET6

class TestOspfv3LinePtpIpv4(TestOspfv3Line):
    network = 'point-to-point'
    numnodes = 4
    af = ipaddr.AF_INET

class TestOspfv3LinePtpIpv6(TestOspfv3Line):
    network = 'point-to-point'
    numnodes = 4
    af = ipaddr.AF_INET6

def suite():
    return quagga.test.makeSuite(TestOspfv3LineIpv4, TestOspfv3LineIpv6,
                                 TestOspfv3LinePtpIpv4, TestOspfv3LinePtpIpv6)

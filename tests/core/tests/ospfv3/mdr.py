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
import quagga.topology.wlan
import ospfv3

from core.misc import ipaddr

class TestOspfv3Mdr(quagga.test.QuaggaTestCase):
    'test OSPFv3 MANET MDR routing for a random topology'

    numnodes = None
    af = None

    stableDuration = 10
    stableWait = 60

    quagga_conf_template = '''\
debug ospf6 neighbor state
debug ospf6 mdr
!
interface eth0
  ipv6 ospf6 network manet-designated-router
  ipv6 ospf6 twohoprefresh 3
  ipv6 ospf6 instance-id %(instanceid)s
!
router ospf6
  router-id %(routerid)s
  interface eth0 area 0.0.0.0
'''

    def setUp(self):
        assert self.numnodes is not None
        assert self.af is not None

        if self.af == ipaddr.AF_INET6:
            instanceid = 0
        elif self.af == ipaddr.AF_INET:
            instanceid = 64
        else:
            raise ValueError, 'invalid af: %s' % self.af

        self.topology = quagga.topology.wlan.Wlan(self.numnodes)

        for i in xrange(self.numnodes):
            d = {'instanceid': instanceid,
                 'routerid': quagga.node.RouterId(i + 1),
                 }
            self.topology.n[i].quagga_conf = self.quagga_conf_template % d

        self.topology.startup()

    def CheckOspfv3Stable(self):
        # wait to converge
        stable = \
            ospfv3.Ospfv3StableBarrier(self.topology,
                                       stableDuration = self.stableDuration)
        stable.Wait(self.stableWait)

    def CheckOspfv3Neighbors(self):
        for n in self.topology.n:
            nbrs = n.Ospfv3Neighbors()
            nbrfull = None
            for rid in nbrs:
                state = nbrs[rid].state
                assert state in ('Full', 'Twoway'), \
                    '%s: incorrect state for neighbor %s: %s' % \
                    (n.name, rid, state)
                if state == 'Full':
                    nbrfull = rid
            assert nbrfull is not None, \
                '%s: no Full neighbor found' % n.name

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

    def test_mdr(self):
        'ospfv3 mdr: check convergence, neighbor state, and routes'

        self.CheckOspfv3Stable()

        self.CheckOspfv3Neighbors()

        allprefixes = filter(lambda x: x.af == self.af,
                             self.topology.AllPrefixes())
        self.CheckOspfv3Routes(allprefixes)
        self.CheckZebraRoutes(allprefixes)
        self.CheckKernelRoutes(allprefixes)

        self.CheckPing()

class TestOspfv3MdrIpv4(TestOspfv3Mdr):
    'test OSPFv3 MANET MDR routing for a random topology'
    numnodes = 20
    af = ipaddr.AF_INET

class TestOspfv3MdrIpv6(TestOspfv3Mdr):
    'test OSPFv3 MANET MDR routing for a random topology'
    numnodes = 20
    af = ipaddr.AF_INET6

def suite():
    return quagga.test.makeSuite(TestOspfv3MdrIpv4, TestOspfv3MdrIpv6)

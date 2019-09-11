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
import quagga.topology.grid
import ospfv3

from core.misc import ipaddr

class TestOspfv3Grid(quagga.test.QuaggaTestCase):
    '''test OSPFv3 routing for a mxn grid topology

    test topology:

       n_1 ---- n_2 ..... n_m
        |        .         |
       n_m+1 ... . ...... n_2*m
        .        .         .
      n_m*(n-1)  . ...... n_m*n
    '''

    m = None
    n = None
    network = None
    af = None

    stableDuration = 10
    stableWait = 60

    quagga_conf_template_common = '''\
debug ospf6 neighbor state
'''

    quagga_conf_template_interface = '''\
interface %(ifname)s
  ipv6 ospf6 network %(network)s
  ipv6 ospf6 instance-id %(instanceid)s
'''

    quagga_conf_template_router = '''\
router ospf6
  router-id %(routerid)s
'''

    def setUp(self):
        assert self.n is not None
        assert self.m is not None
        assert self.network is not None
        assert self.af is not None

        if self.af == ipaddr.AF_INET6:
            instanceid = 0
        elif self.af == ipaddr.AF_INET:
            instanceid = 64
        else:
            raise ValueError, 'invalid af: %s' % self.af

        if self.network == 'point-to-point':
            prefixkwds = {'ipv4prefix': '10.0.0.0/30', 'ipv6prefix': 'a::/126'}
        elif self.network == 'broadcast':
            prefixkwds = {'ipv4prefix': '10.0.0.0/24', 'ipv6prefix': 'a::/64'}
        else:
            prefixkwds = {}

        self.topology = quagga.topology.grid.Grid(self.m, self.n, **prefixkwds)

        def QuaggaConf(node):
            quagga_conf = self.quagga_conf_template_common + '!\n'
            for netif in node.netifs():
                ifconfig = self.quagga_conf_template_interface % \
                    {'ifname': netif.name,
                     'network': self.network,
                     'instanceid': instanceid}
                quagga_conf += ifconfig + '!\n'
            quagga_conf += self.quagga_conf_template_router % \
                {'routerid': quagga.node.RouterId(node.nodeid())}
            for netif in node.netifs():
                quagga_conf += '  interface %s area 0.0.0.0\n' % netif.name
            return quagga_conf

        for node in self.topology.n:
            node.quagga_conf = QuaggaConf(node)

        self.topology.startup()

    def CheckOspfv3Stable(self):
        # wait to converge
        stable = \
            ospfv3.Ospfv3StableBarrier(self.topology,
                                       stableDuration = self.stableDuration)
        stable.Wait(self.stableWait)

    def CheckOspfv3Neighbors(self):
        for i in xrange(self.n):
            for j in xrange(self.m):
                node = self.topology.n[i * self.n + j]
                expected_nbrs = []
                ifindex = 0

                def ExpectedNeighbor(nbrnode, ifindex, expected_nbrs):
                    nbr = (str(quagga.node.RouterId(nbrnode.nodeid())),
                           'eth%s' % ifindex)
                    ifindex += 1
                    expected_nbrs.append(nbr)
                    return ifindex, expected_nbrs

                if j > 0:
                    ifindex, expected_nbrs = \
                        ExpectedNeighbor(self.topology.n[i * self.n + j - 1],
                                         ifindex, expected_nbrs)

                if i > 0:
                    ifindex, expected_nbrs = \
                        ExpectedNeighbor(self.topology.n[(i - 1) * self.n + j],
                                         ifindex, expected_nbrs)

                if i < self.n - 1:
                    ifindex, expected_nbrs = \
                        ExpectedNeighbor(self.topology.n[(i + 1) * self.n + j],
                                         ifindex, expected_nbrs)

                if j < self.m - 1:
                    ifindex, expected_nbrs = \
                        ExpectedNeighbor(self.topology.n[i * self.n + j + 1],
                                         ifindex, expected_nbrs)

            actual_nbrs = node.Ospfv3Neighbors()
            for nbrid, nbrif in expected_nbrs:
                node.CheckNeighbor(actual_nbrs, nbrid, nbrif, 'Full')

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

    def test_grid(self):
        'ospfv3: grid test: check convergence, neighbor state, and routes'

        self.CheckOspfv3Stable()

        self.CheckOspfv3Neighbors()

        allprefixes = filter(lambda x: x.af == self.af,
                             self.topology.AllPrefixes())

        self.CheckOspfv3Routes(allprefixes)
        self.CheckZebraRoutes(allprefixes)
        self.CheckKernelRoutes(allprefixes)

        self.CheckPing()

class TestOspfv3LineIpv4(TestOspfv3Grid):
    network = 'broadcast'
    m = 8
    n = 1
    af = ipaddr.AF_INET

class TestOspfv3LineIpv6(TestOspfv3Grid):
    network = 'broadcast'
    m = 8
    n = 1
    af = ipaddr.AF_INET6

class TestOspfv3LinePtpIpv4(TestOspfv3Grid):
    network = 'point-to-point'
    m = 8
    n = 1
    af = ipaddr.AF_INET

class TestOspfv3LinePtpIpv6(TestOspfv3Grid):
    network = 'point-to-point'
    m = 8
    n = 1
    af = ipaddr.AF_INET6

class TestOspfv3GridIpv4(TestOspfv3Grid):
    network = 'broadcast'
    m = 4
    n = 4
    af = ipaddr.AF_INET

class TestOspfv3GridIpv6(TestOspfv3Grid):
    network = 'broadcast'
    m = 4
    n = 4
    af = ipaddr.AF_INET6

class TestOspfv3GridPtpIpv4(TestOspfv3Grid):
    network = 'point-to-point'
    m = 4
    n = 4
    af = ipaddr.AF_INET

class TestOspfv3GridPtpIpv6(TestOspfv3Grid):
    network = 'point-to-point'
    m = 4
    n = 4
    af = ipaddr.AF_INET6

class TestOspfv3LargeGridIpv4(TestOspfv3Grid):
    network = 'broadcast'
    m = 8
    n = 8
    af = ipaddr.AF_INET

    stableDuration = 30
    stableWait = 120

def suite():
    tests = (TestOspfv3LineIpv4, TestOspfv3LineIpv6,
             TestOspfv3LinePtpIpv4, TestOspfv3LinePtpIpv6,
             TestOspfv3GridIpv4, TestOspfv3GridIpv6,
             TestOspfv3GridPtpIpv4, TestOspfv3GridPtpIpv6,
             TestOspfv3LargeGridIpv4)
    return quagga.test.makeSuite(*tests)

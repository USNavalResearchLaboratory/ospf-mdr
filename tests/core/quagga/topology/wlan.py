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

from topology import Topology
from core import pycore
from core.misc import ipaddr
import random

class Wlan(Topology):
    def __init__(self, numnodes, linkprob = 0.35, seed = None,
                 ipv4prefix = '10.0.0.0/8', ipv6prefix = 'a::/64'):
        assert numnodes > 1
        Topology.__init__(self, numnodes)

        if seed is not None:
            random.seed(seed)

        self.net = self.session.addobj(cls = pycore.nodes.WlanNode)

        p4 = ipaddr.IPv4Prefix(ipv4prefix)
        p6 = ipaddr.IPv6Prefix(ipv6prefix)

        for i in xrange(numnodes):
            addrlist = ['%s/%s' % (p4.addr(i + 1), 32),
                        '%s/%s' % (p6.addr(i + 1), 128)]
            self.n[i].newnetif(self.net, addrlist = addrlist, ifname = 'eth0')

        # connect nodes with probability linkprob
        for i in xrange(numnodes):
            netif = self.n[i].netif(0)
            for j in xrange(i + 1, numnodes):
                r = random.random()
                if r < linkprob:
                    self.net.link(netif, self.n[j].netif(0))
            if not self.net._linked[netif]:
                # force one link to avoid partitions
                j = i
                while j == i:
                    j = random.randint(0, numnodes - 1)
                self.net.link(netif, self.n[j].netif(0))

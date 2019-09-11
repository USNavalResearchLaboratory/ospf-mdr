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

class Grid(Topology):
    def __init__(self, m, n,
                 ipv4prefix = '10.0.0.0/30', ipv6prefix = 'a::/126'):
        assert m > 1
        assert n > 0
        Topology.__init__(self, m * n)

        p4 = ipaddr.IPv4Prefix(ipv4prefix)
        p6 = ipaddr.IPv6Prefix(ipv6prefix)

        class NetConfig(object):
            def __init__(self, net, ipv4prefix, ipv6prefix):
                self.net = net
                self.ipv4prefix = ipv4prefix
                self.ipv6prefix = ipv6prefix

        for i in xrange(n):
            for j in xrange(m):
                node = self.n[i * n + j]
                ifindex = 0

                # network above
                if j > 0:
                    above = self.n[i * n + j - 1]
                    netcfg = above.netcfg_below
                    addrlist = ['%s/%s' % (netcfg.ipv4prefix.addr(2),
                                           netcfg.ipv4prefix.prefixlen),
                                '%s/%s' % (netcfg.ipv6prefix.addr(2),
                                           netcfg.ipv6prefix.prefixlen)]
                    node.newnetif(netcfg.net, addrlist = addrlist,
                                  ifname = 'eth%s' % ifindex)
                    ifindex += 1
                    node.netcfg_above = above.netcfg_below

                # network to the left
                if i > 0:
                    left = self.n[(i - 1) * n + j]
                    netcfg = left.netcfg_right
                    addrlist = ['%s/%s' % (netcfg.ipv4prefix.addr(2),
                                           netcfg.ipv4prefix.prefixlen),
                                '%s/%s' % (netcfg.ipv6prefix.addr(2),
                                           netcfg.ipv6prefix.prefixlen)]
                    node.newnetif(netcfg.net, addrlist = addrlist,
                                  ifname = 'eth%s' % ifindex)
                    ifindex += 1
                    node.netcfg_left = left.netcfg_right

                # network to the right
                if i < n - 1:
                    net = self.session.addobj(cls = pycore.nodes.SwitchNode)
                    netcfg = NetConfig(net, p4, p6)
                    p4 += 1
                    p6 += 1
                    addrlist = ['%s/%s' % (netcfg.ipv4prefix.addr(1),
                                           netcfg.ipv4prefix.prefixlen),
                                '%s/%s' % (netcfg.ipv6prefix.addr(1),
                                           netcfg.ipv6prefix.prefixlen)]
                    node.newnetif(netcfg.net, addrlist = addrlist,
                                  ifname = 'eth%s' % ifindex)
                    ifindex += 1
                    node.netcfg_right = netcfg

                # network below
                if j < m - 1:
                    net = self.session.addobj(cls = pycore.nodes.SwitchNode)
                    netcfg = NetConfig(net, p4, p6)
                    p4 += 1
                    p6 += 1
                    addrlist = ['%s/%s' % (netcfg.ipv4prefix.addr(1),
                                           netcfg.ipv4prefix.prefixlen),
                                '%s/%s' % (netcfg.ipv6prefix.addr(1),
                                           netcfg.ipv6prefix.prefixlen)]
                    node.newnetif(netcfg.net, addrlist = addrlist,
                                  ifname = 'eth%s' % ifindex)
                    ifindex += 1
                    node.netcfg_below = netcfg

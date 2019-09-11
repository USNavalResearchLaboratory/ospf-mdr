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

class Line(Topology):
    def __init__(self, numnodes,
                 ipv4prefix = '10.0.0.0/30', ipv6prefix = 'a::/126'):
        assert numnodes > 1
        Topology.__init__(self, numnodes)

        self.net = []
        p4 = ipaddr.IPv4Prefix(ipv4prefix)
        p6 = ipaddr.IPv6Prefix(ipv6prefix)

        self.net.append(self.session.addobj(cls = pycore.nodes.SwitchNode))
        addrlist = ['%s/%s' % (p4.addr(1), p4.prefixlen),
                    '%s/%s' % (p6.addr(1), p6.prefixlen)]
        self.n[0].newnetif(self.net[0], addrlist = addrlist, ifname = 'eth0')

        for i in xrange(1, numnodes - 1):
            addrlist = ['%s/%s' % (p4.addr(2), p4.prefixlen),
                        '%s/%s' % (p6.addr(2), p6.prefixlen)]
            self.n[i].newnetif(self.net[i - 1],
                               addrlist = addrlist, ifname = 'eth0')
            p4 += 1
            p6 += 1
            self.net.append(self.session.addobj(cls = pycore.nodes.SwitchNode))
            addrlist = ['%s/%s' % (p4.addr(1), p4.prefixlen),
                        '%s/%s' % (p6.addr(1), p6.prefixlen)]
            self.n[i].newnetif(self.net[i],
                               addrlist = addrlist, ifname = 'eth1')

        i += 1
        addrlist = ['%s/%s' % (p4.addr(2), p4.prefixlen),
                    '%s/%s' % (p6.addr(2), p6.prefixlen)]
        self.n[i].newnetif(self.net[i - 1],
                           addrlist = addrlist, ifname = 'eth0')

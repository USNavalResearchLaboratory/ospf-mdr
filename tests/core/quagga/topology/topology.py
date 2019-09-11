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

import quagga.node
from core import pycore
from core.misc import ipaddr

def ipprefix(prefixstr):
    for af in ipaddr.AF_INET, ipaddr.AF_INET6:
        try:
            return ipaddr.IPPrefix(af, prefixstr)
        except:
            pass
    return None

class Topology(object):
    'base class for topologies: a CORE session and a collection of nodes'

    def __init__(self, numnodes, nodecls = quagga.node.QuaggaTestNode):
        assert numnodes >= 0
        self.session = pycore.Session()
        self.n = []
        for i in xrange(numnodes):
            nodeid = i + 1
            n = self.session.addobj(cls = nodecls,
                                    name = 'n%s' % nodeid, nodeid = nodeid)
            self.n.append(n)

    def shutdown(self):
        self.session.shutdown()

    def startup(self):
        for n in self.n:
            n.boot()

    def AllPrefixes(self):
        prefixes = []
        prefix_strings = set()
        for n in self.n:
            netifs = n.netifs()
            for netif in netifs:
                for addr in netif.addrlist:
                    prefix = ipprefix(addr)
                    assert prefix is not None
                    pstring = str(prefix)
                    if pstring not in prefix_strings:
                        prefixes.append(prefix)
                        prefix_strings.add(pstring)
        prefixes.sort(cmp = lambda x, y: cmp(str(x), str(y)))
        return prefixes

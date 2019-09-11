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

import os
from core import pycore
from core.constants import *
from core.misc import ipaddr

class Ospfv3Neighbor(object):
    def __init__(self, routerid, priority, deadtime, state, nbrifstate,
                 duration, ifname, ifstate):
        def time2sec(s):
            h, m, s = map(int, s.split(':'))
            return s + 60 * (m + 60 * h)
        self.routerid = routerid
        self.priority = int(priority)
        self.deadtime = time2sec(deadtime)
        self.state = state
        self.nbrifstate = nbrifstate
        self.duration = time2sec(duration)
        self.ifname = ifname
        self.ifstate = ifstate

    def InState(self, *states):
        states = map(str.lower, states)
        return self.state.lower() in states

class Route(object):
    def __init__(self, destprefix, outif, nexthop = None):
        self.destprefix = destprefix
        self.nexthop = nexthop
        self.outif = outif

    def __str__(self):
        return '%s via %s dev %s' % \
            (self.destprefix, self.nexthop, self.outif)

class Ospfv3Route(Route):
    def __init__(self, destprefix, outif, nexthop,
                 best, desttype, routetype, duration):
        Route.__init__(self, destprefix, outif, nexthop)
        self.best = best
        self.desttype = desttype
        self.routetype = routetype
        self.duration = duration

class ZebraRoute(Route):
    def __init__(self, destprefix, outif, nexthop,
                 fib, selected, routetype, metric, duration):
        Route.__init__(self, destprefix, outif, nexthop)
        self.fib = fib
        self.selected = selected
        self.routetype = routetype
        self.metric = metric
        self.duration = duration

class KernelRoute(Route):
    def __init__(self, destprefix, outif, nexthop = None,
                 protocol = None, scope = None, metric = None):
        Route.__init__(self, destprefix, outif, nexthop)
        self.metric = metric
        self.protocol = protocol
        self.scope = scope

class QuaggaNode(pycore.nodes.LxcNode):
    CONFDIR = ['/etc/quagga', '/usr/local/etc/quagga']

    def __init__(self, session, name, nodeid = None):
        self.confdir = None
        for d in self.CONFDIR:
            if os.path.isdir(d):
                self.confdir = d
                break
        assert self.confdir is not None, 'no quagga config directory found'
        pycore.nodes.LxcNode.__init__(self, session = session,
                                      name = name, objid = nodeid)

    def startup(self):
        pycore.nodes.LxcNode.startup(self)
        self.privatedir(self.confdir)

    def boot(self):
        self.config()
        cmd = ('/bin/sh', self.bootsh)
        self.cmd(cmd)

    def quagga_conf_file(self):
        raise NotImplementedError

    def config(self):
        # create Quagga configuration
        filename = os.path.join(self.confdir, 'Quagga.conf')
        f = self.opennodefile(filename, 'w')
        f.write(self.quagga_conf_file())
        f.close()
        # create startup script
        f = self.opennodefile(self.bootsh, 'w')
        f.write(self.bootscript())
        f.close()

    def bootscript(self):
        return '''\
#!/bin/sh -e

STATEDIR=%s

IPV4_PARAM_ENABLE="forwarding"
IPV4_PARAM_DISABLE="rp_filter send_redirects secure_redirects accept_redirects"

IPV6_PARAM_ENABLE="forwarding"
IPV6_PARAM_DISABLE="accept_redirects"

echo 1 > /proc/sys/net/ipv4/ip_forward

set_kernel_parameters()
{
    local pathprefix param val
    pathprefix=$1
    parameters=$2
    val=$3

    for param in $parameters; do
        for f in $pathprefix/*/$param; do
            echo $val > $f
        done
    done
}

set_kernel_parameters /proc/sys/net/ipv4/conf "$IPV4_PARAM_ENABLE" 1
set_kernel_parameters /proc/sys/net/ipv4/conf "$IPV4_PARAM_DISABLE" 0

set_kernel_parameters /proc/sys/net/ipv6/conf "$IPV6_PARAM_ENABLE" 1
set_kernel_parameters /proc/sys/net/ipv6/conf "$IPV6_PARAM_DISABLE" 0

waitfile()
{
    local fname
    fname=$1

    i=0
    until [ -e $fname ]; do
        i=$(($i + 1))
        if [ $i -eq 10 ]; then
            echo "file not found: $fname" >&2
            exit 1
        fi
        sleep 0.1
    done
}

mkdir -p $STATEDIR

zebra -d -u root -g root
waitfile $STATEDIR/zebra.vty

ospf6d -d -u root -g root
waitfile $STATEDIR/ospf6d.vty

vtysh -b
''' % QUAGGA_STATE_DIR

    def cmdoutput(self, cmdargs):
        status, output = self.cmdresult(cmdargs)
        assert status == 0, \
            '%s: %s: nonzero exit status: %s\n  command output: %s' % \
            (self.name, cmdargs, status, output)
        return output

    def Address(self, af, ifnum = 0):
        for a in self.netif(0).addrlist:
            addr, plen = a.split('/')
            addr = ipaddr.IPAddr.fromstring(addr)
            if addr.af == af:
                return addr
        return None

    def Ospfv3Neighbors(self):
        nbrs = {}
        cmd = ('vtysh', '-c', 'show ipv6 ospf6 neighbor')
        output = self.cmdoutput(cmd)
        lines = output.split('\n')[1:]
        for line in lines:
            if not line:
                continue
            nbrid, prio, deadtime, state, duration, ifname = line.split()
            # XXX
            assert nbrid not in nbrs, \
                'can\'t handle same neighbor on multiple interfaces'
            state, nbrifstate = state.split('/')
            ifname, ifstate = ifname.split('[')
            ifstate = ifstate[:-1]
            nbrs[nbrid] = Ospfv3Neighbor(nbrid, prio, deadtime, state,
                                         nbrifstate, duration, ifname, ifstate)
        return nbrs

    def Ospfv3Routes(self):
        routes = []
        cmd = ('vtysh', '-c', 'show ipv6 ospf6 route')
        output = self.cmdoutput(cmd)
        lines = output.split('\n')
        for line in lines:
            if not line:
                continue
            tmp = line.split()
            if len(tmp) != 6:
                continue # multipath nexthop entry or other unrecognized line
            desttype, routetype, destprefix, nexthop, outif, duration = tmp
            if desttype[0] == '*':
                best = True
                desttype = desttype[1:]
            else:
                best = False
            routes.append(Ospfv3Route(destprefix, outif, nexthop,
                                      best, desttype, routetype, duration))
        return routes

    def ZebraRoutes(self, ipv6 = False):
        routes = []
        if ipv6:
            cmd = ('vtysh', '-c', 'show ipv6 route')
        else:
            cmd = ('vtysh', '-c', 'show ip route')
        output = self.cmdoutput(cmd)
        lines = output.split('\n')[1:]
        for line in lines:
            if not line or line[0].isspace():
                continue
            routetype, destprefix, nexthop = line.split(None, 2)
            if routetype in ('*', 'via'):
                continue        # multipath nexthop
            nexthop, outif = nexthop.split(', ', 1)
            if nexthop == 'is directly connected':
                nexthop = None
                metric = 1
            else:
                tmp = nexthop.split()
                metric = tmp[0][1:-1]
                nexthop = tmp[2]
            if ',' in outif:
                outif, duration = outif.split(', ')
            else:
                duration = None
            if routetype[-1] == '*':
                fib = True
                routetype = routetype[:-1]
            else:
                fib = False
            if routetype[-1] == '>':
                selected = True
                routetype = routetype[:-1]
            else:
                selected = False
            routes.append(ZebraRoute(destprefix, outif, nexthop, fib,
                                     selected, routetype, metric, duration))
        return routes

    def KernelRoutes(self, ipv6 = False):
        routes = []
        if ipv6:
            cmd = ('ip', '-6', 'route')
        else:
            cmd = ('ip', 'route')
        output = self.cmdoutput(cmd)
        lines = output.split('\n')
        for line in lines:
            if not line:
                continue
            tmp = line.split()
            destprefix = tmp.pop(0)
            if destprefix == 'unreachable':
                continue
            if '/' not in destprefix:
                if ipv6:
                    destprefix += '/128'
                else:
                    destprefix += '/32'
            nexthop = outif = protocol = scope = metric = None
            while tmp:
                key = tmp.pop(0)
                val = tmp.pop(0)
                if key == 'via':
                    nexthop = val
                elif key == 'dev':
                    outif = val
                elif key == 'proto':
                    protocol = val
                elif key == 'scope':
                    scope = val
                elif key == 'metric':
                    metric= val
                else:
                    pass
            routes.append(KernelRoute(destprefix, outif, nexthop,
                                      protocol, scope, metric))
        return routes

    def CheckNeighbor(self, nbrs, nbrid, ifname, *nbrstates):
        'check that nbrid is in nbtstates on interface ifname'
        assert nbrid in nbrs, \
            '%s: neighbor not found: %s' % (self.name, nbrid)
        nbr = nbrs[nbrid]
        assert nbr.InState(*nbrstates), \
            '%s: neighbor %s state mismatch: %s [expected state(s) %s]' % \
            (self.name, nbrid, nbr.state, nbrstates)
        assert nbr.ifname == ifname, \
            '%s: neighbor %s interface mismatch: %s [expected interface %s]' % \
            (self.name, nbrid, nbr.ifname, ifname)

    def CheckOspfv3Routes(self, prefixes):
        'check that an (exact) ospfv3 route exists for each prefix in prefixes'
        routes = filter(lambda x: x.best, self.Ospfv3Routes())
        route_prefixes = map(lambda x: x.destprefix, routes)
        for p in map(str, prefixes):
            assert p in route_prefixes, \
                '%s: no ospfv3 route to prefix %s' % (self.name, p)

    def CheckZebraRoutes(self, prefixes, ipv6):
        'check that an (exact) zebra route exists for each prefix in prefixes'
        routes = filter(lambda x: x.selected, self.ZebraRoutes(ipv6))
        route_prefixes = map(lambda x: x.destprefix, routes)
        for p in map(str, prefixes):
            assert p in route_prefixes, \
                '%s: no zebra route to prefix %s' % (self.name, p)

    def CheckKernelRoutes(self, prefixes, ipv6):
        'check that an (exact) kernel route exists for each prefix in prefixes'
        routes = self.KernelRoutes(ipv6)
        route_prefixes = map(lambda x: x.destprefix, routes)
        addrlist = []
        for netif in self.netifs():
            addrlist += netif.addrlist
        for p in map(str, prefixes):
            if p in addrlist:
                continue
            assert p in route_prefixes, \
                '%s: no kernel route to prefix %s' %  (self.name, p)

    def CheckPing(self, n, af):
        'check that this node can ping node n'
        addr = n.Address(af)
        assert addr is not None
        if af == ipaddr.AF_INET6:
            cmd = ('ping6',)
        else:
            cmd = ('ping',)
        cmd += ('-l', '3', '-w', '5', '-c', '1', str(addr))
        status, output = self.cmdresult(cmd)
        assert status == 0, \
            '%s: ping %s failed: %s' % (self.name, addr, output)

class QuaggaTestNode(QuaggaNode):
    'base class for nodes running quagga'

    def __init__(self, *args, **kwds):
        QuaggaNode.__init__(self, *args, **kwds)
        self.quagga_conf = ''

    def quagga_conf_file(self):
        common = '''\
log file /var/log/quagga.log
!
log timestamp precision 4
!
ip forwarding
ipv6 forwarding
!
'''
        return common + self.quagga_conf

class RouterId(object):
    def __init__(self, routerid):
        if isinstance(routerid, str):
            self.routerid = ipaddr.IPAddr.fromstring(routerid)
        elif isinstance(routerid, int):
            self.routerid = ipaddr.IPAddr(ipaddr.AF_INET,
                                          ipaddr.struct.pack('!L', routerid))
        else:
            msg = 'expected a string or integer router-id not \'%s\'' % \
                str(routerid)
            assert False, msg

    def __str__(self):
        return str(self.routerid)

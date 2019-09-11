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

import quagga.barrier
import quagga.event

class Ospfv3StableBarrier(object):

    class Ospfv3StableCondition(quagga.barrier.Condition):
        def __init__(self, barrier, node, eventLoop, stableDuration,
                     minNeighbors = 1, pollInterval = 3):
            self.barrier = barrier
            self.node = node
            self.eventLoop = eventLoop
            self.stableDuration = stableDuration
            self.minNeighbors = minNeighbors
            self.pollInterval = pollInterval
            self.eventLoop.AddEvent(self.pollInterval, self.Check)

        def Check(self):
            if self.IsTrue():
                self.barrier.Notify(self)
            else:
                self.eventLoop.AddEvent(self.pollInterval, self.Check)

        def IsTrue(self):
            neighbors = self.node.Ospfv3Neighbors()
            if len(neighbors) < self.minNeighbors:
                return False
            for nbr in neighbors.values():
                if nbr.ifstate == 'Waiting' or \
                        nbr.duration < self.stableDuration:
                    return False
            return True

    def __init__(self, topology, stableDuration = 10):
        self.topology = topology
        self.stableDuration = stableDuration

    def Wait(self, timeout):
        evloop = quagga.event.EventLoop()

        def ConvergenceTimeout():
            evloop.Stop()
            assert False, 'OSPFv3 failed to converge'

        barrier = quagga.barrier.ConditionBarrier(ConvergenceTimeout)
        for node in self.topology.n:
            cond = self.Ospfv3StableCondition(barrier, node, evloop,
                                              self.stableDuration)
            barrier.AddCondition(cond)

        evloop.Run()
        barrier.Wait(timeout)
        evloop.Stop()

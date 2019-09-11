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

import threading
import time

class Condition(object):
    def IsTrue(self):
        raise NotImplementedError

class ConditionBarrier(object):
    def __init__(self, timeoutfunc = None, *timeoutargs, **timeoutkwds):
        self.tofunc = timeoutfunc
        self.toargs = timeoutargs
        self.tokwds = timeoutkwds
        self.cond = threading.Condition()
        self.conditions = {}
        self.satisfied = {}
        self.complete = False

    def Wait(self, timeout = None):
        if timeout is None:
            to = None
        else:
            to = time.time() + timeout
        self.cond.acquire()
        while not self.__satisfied():
            if to is None:
                remaining = None
            else:
                remaining = to - time.time()
                if remaining <= 0.0:
                    break
            self.cond.wait(remaining)
        satisfied = self.__satisfied()
        self.cond.release()
        if not satisfied:
            self.Timeout()

    def __satisfied(self):
        self.cond.acquire()
        if not self.conditions:
            self.complete = True
        else:
            for cond in self.conditions.values():
                if cond.IsTrue():
                    self.__condition_satisfied(cond)
                else:
                    break
        satisfied = self.complete
        self.cond.release()
        return satisfied

    def __condition_satisfied(self, condition):
        del self.conditions[condition]
        self.satisfied[condition] = condition
        if not self.conditions:
            self.complete = True
            self.cond.notifyAll()

    def Notify(self, condition):
        self.cond.acquire()
        if condition in self.satisfied:
            assert condition.IsTrue()
        elif condition not in self.conditions:
            self.cond.release()
            raise ValueError, 'unknown condition: %s' % condition
        else:
            if condition.IsTrue():
                self.__condition_satisfied(condition)
        self.cond.release()

    def AddCondition(self, condition):
        self.cond.acquire()
        if self.complete:
            self.cond.release()
            raise ValueError, 'all conditions already satisfied'
        self.conditions[condition] = condition
        self.cond.release()

    def Timeout(self):
        if not self.tofunc:
            raise NotImplementedError
        self.tofunc(*self.toargs, **self.tokwds)

def example():
    barrier = ConditionBarrier()
    barrier.Wait()
    try:
        barrier.AddCondition(None)
    except ValueError:
        pass

    class TimeElapsedCondition(Condition):
        def __init__(self, elapsed, barrier = None):
            self.done = time.time() + elapsed
            self.barrier = barrier
            if self.barrier:
                self.timer = threading.Timer(elapsed, self.NotifyBarrier)
                self.timer.daemon = True
                self.timer.start()

        def NotifyBarrier(self):
            if not self.barrier:
                return
            done = self.IsTrue()
            if done:
                self.barrier.Notify(self)

        def IsTrue(self):
            now = time.time()
            return now >= self.done

    barrier = ConditionBarrier()
    cond = TimeElapsedCondition(5)
    barrier.AddCondition(cond)
    barrier.Wait(10)

    barrier = ConditionBarrier()
    cond = TimeElapsedCondition(2, barrier)
    barrier.AddCondition(cond)
    barrier.Wait(10)

// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_ROUTER_CONFIG_HH_
#define _ZEBRA_ROUTER_CONFIG_HH_


class ZebraVifConfig {

public:

    virtual void clear_all_applied() const = 0;
};

class ZebraConfig {

public:

    ZebraConfig() : _is_applied(false) {}

    bool is_applied() const {return _is_applied;}
    void set_applied() const {_is_applied = true;}
    void clear_applied() const {_is_applied = false;}

private:

    mutable bool _is_applied;
};

template<class T>
class ZebraConfigVal : public ZebraConfig {

public:

    ZebraConfigVal() : _value(), _is_set(false) {}
    ZebraConfigVal(const T& value) : _value(value), _is_set(true) {}

    bool is_set() const {return _is_set;}
    const T &get() const
    {
	if (!_is_set)
	    throw "value is not set";
	return _value;
    }
    void set(const T &value)
    {
	_value = value;
	_is_set = true;
    }
    void invalidate() {_is_set = false;}

    bool operator<(const ZebraConfigVal<T>& other) const
    {
	return  _value < other._value;
    }

private:

    T _value;
    bool _is_set;
};

#endif	// _ZEBRA_ROUTER_CONFIG_HH_

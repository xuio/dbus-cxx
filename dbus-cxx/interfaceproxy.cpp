/***************************************************************************
 *   Copyright (C) 2007,2008,2009 by Rick L. Vinyard, Jr.                  *
 *   rvinyard@cs.nmsu.edu                                                  *
 *                                                                         *
 *   This file is part of the dbus-cxx library.                            *
 *                                                                         *
 *   The dbus-cxx library is free software; you can redistribute it and/or *
 *   modify it under the terms of the GNU General Public License           *
 *   version 3 as published by the Free Software Foundation.               *
 *                                                                         *
 *   The dbus-cxx library is distributed in the hope that it will be       *
 *   useful, but WITHOUT ANY WARRANTY; without even the implied warranty   *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   *
 *   General Public License for more details.                              *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this software. If not see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/
#include "interfaceproxy.h"
#include <map>
#include <utility>
#include "connection.h"
#include "methodproxybase.h"
#include "objectproxy.h"
#include <sigc++/sigc++.h>
#include "signal_proxy_base.h"

namespace DBus
{

  class CallMessage;
  class PendingCall;
  class ReturnMessage;

  InterfaceProxy::InterfaceProxy( const std::string& name ):
      m_object(nullptr),
      m_name(name)
  {
  }

  std::shared_ptr<InterfaceProxy> InterfaceProxy::create(const std::string& name)
  {
    return std::shared_ptr<InterfaceProxy>( new InterfaceProxy(name) );
  }

  InterfaceProxy::~ InterfaceProxy( )
  {
  }

  ObjectProxy* InterfaceProxy::object() const
  {
    return m_object;
  }

  Path InterfaceProxy::path() const
  {
    if ( m_object ) return m_object->path();
    return Path();
  }

  std::weak_ptr<Connection> InterfaceProxy::connection() const
  {
    if ( m_object ) return m_object->connection();
    return std::weak_ptr<Connection>();
  }

  const std::string & InterfaceProxy::name() const
  {
    return m_name;
  }

  const InterfaceProxy::Methods & InterfaceProxy::methods() const
  {
    return m_methods;
  }

  std::shared_ptr<MethodProxyBase> InterfaceProxy::method( const std::string& name ) const
  {
    Methods::const_iterator iter;
    std::shared_lock lock( m_methods_rwlock );
    
    iter = m_methods.find( name );

    if ( iter == m_methods.end() ) return std::shared_ptr<MethodProxyBase>();

    return iter->second;
  }

  bool InterfaceProxy::add_method( std::shared_ptr<MethodProxyBase> method )
  {
    bool result = true;
    
    if ( not method ) return false;

    if ( this->has_method(method) ) return false;

    if ( method->m_interface ) method->m_interface->remove_method(method);
    
    {
      std::unique_lock lock( m_methods_rwlock );

        m_methods.insert(std::make_pair(method->name(), method));
        method->m_interface = this;
    }

    return result;
  }

  void InterfaceProxy::remove_method( const std::string & name )
  {
    Methods::iterator iter;
    std::shared_ptr<MethodProxyBase> method;

    {
      std::unique_lock lock( m_methods_rwlock );

      iter = m_methods.find( name );
      if ( iter != m_methods.end() ) {
        method = iter->second;
        m_methods.erase( iter );
      }
    }
    method->m_interface = nullptr;
  }

  void InterfaceProxy::remove_method( std::shared_ptr<MethodProxyBase> method )
  {
    Methods::iterator location;
    bool erased = false;

    if ( not method ) return;

    {
      std::unique_lock lock( m_methods_rwlock );

      location = m_methods.find( method->name() );

      if ( location != m_methods.end() )
      {
            m_methods.erase(location);
            erased = true;
      }
    }

    method->m_interface = nullptr;
  }

  bool InterfaceProxy::has_method( const std::string & name ) const
  {
    Methods::const_iterator iter;
    std::shared_lock lock( m_methods_rwlock );

    iter = m_methods.find( name );

    return ( iter != m_methods.end() );
  }

  bool InterfaceProxy::has_method( std::shared_ptr<MethodProxyBase> method ) const
  {
    Methods::const_iterator current, upper;
    bool found = false;

    if ( not method ) return false;
    
    std::shared_lock lock( m_methods_rwlock );

    current = m_methods.lower_bound( method->name() );

    if ( current != m_methods.end() )
    {
      upper = m_methods.upper_bound( method->name() );
      for ( ; current != upper; current++ )
      {
        if ( current->second == method )
        {
          found = true;
          break;
        }
      }
    }

    return found;
  }

  std::shared_ptr<CallMessage> InterfaceProxy::create_call_message( const std::string& method_name ) const
  {
    if ( not m_object ) return std::shared_ptr<CallMessage>();
    return m_object->create_call_message( m_name, method_name );
  }

  std::shared_ptr<const ReturnMessage> InterfaceProxy::call( std::shared_ptr<const CallMessage> call_message, int timeout_milliseconds ) const
  {
    if ( not m_object ) return std::shared_ptr<const ReturnMessage>();
    return m_object->call( call_message, timeout_milliseconds );
  }

  std::shared_ptr<PendingCall> InterfaceProxy::call_async( std::shared_ptr<const CallMessage> call_message, int timeout_milliseconds ) const
  {
    if ( not m_object ) return std::shared_ptr<PendingCall>();
    return m_object->call_async( call_message, timeout_milliseconds );
  }

  const InterfaceProxy::Signals& InterfaceProxy::signals() const
  {
    return m_signals;
  }

  std::shared_ptr<signal_proxy_base> InterfaceProxy::signal(const std::string & signame)
  {
    for ( Signals::iterator i = m_signals.begin(); i != m_signals.end(); i++ )
    {
      if ( (*i)->name() == signame ) return *i;
    }
    return std::shared_ptr<signal_proxy_base>();
  }

  bool InterfaceProxy::add_signal(std::shared_ptr<signal_proxy_base> sig, ThreadForCalling calling )
  {
    // is it a valid signal?
    if ( not sig ) return false;

    // is it already added?
    if ( m_signals.find(sig) != m_signals.end() ) return false;

    // make sure the path and interface are set
    sig->set_path( this->path() );
    sig->set_interface( this->name() );

    // add it to the signal set
    m_signals.insert(sig);

    // connect it
    std::shared_ptr<Connection> conn = connection().lock();
    if ( conn and conn->is_valid() ){
      conn->add_signal_proxy(sig, calling);
    }else{
        return false;
    }

    m_callingMap[ sig ] = calling;

    return true;
  }

  bool InterfaceProxy::remove_signal(const std::string & signame)
  {
    return this->remove_signal( this->signal(signame) );
  }

  bool InterfaceProxy::remove_signal(std::shared_ptr<signal_proxy_base> sig)
  {
    if ( not sig ) return false;
    if ( not this->has_signal(sig) ) return false;
    std::shared_ptr<Connection> conn = connection().lock();
    if( conn ){
        conn->remove_signal_proxy(sig);
    }
    m_signals.erase(sig);
    m_callingMap.erase( sig );
    return true;
  }

  bool InterfaceProxy::has_signal(const std::string & signame) const
  {
    for ( Signals::iterator i = m_signals.begin(); i != m_signals.end(); i++ )
    {
      if ( (*i)->name() == signame ) return true;
    }
    return false;
  }

  bool InterfaceProxy::has_signal(std::shared_ptr<signal_proxy_base> sig) const
  {
    return m_signals.find(sig) != m_signals.end();
  }

  void InterfaceProxy::on_object_set_connection(std::shared_ptr< Connection > conn)
  {
    for ( std::shared_ptr<signal_proxy_base> sig : m_signals ){
        ThreadForCalling calling = m_callingMap[ sig ];
        conn->add_signal_proxy( sig, calling );
    }
  }

  void InterfaceProxy::on_object_set_path(const std::string & path)
  {
    for ( Signals::iterator i = m_signals.begin(); i != m_signals.end(); i++ )
      (*i)->set_path(path);
  }

}


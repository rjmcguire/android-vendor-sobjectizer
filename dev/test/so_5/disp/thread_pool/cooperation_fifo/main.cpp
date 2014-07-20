/*
 * A simple test for thread_pool dispatcher.
 */

#include <iostream>
#include <set>
#include <vector>
#include <exception>
#include <stdexcept>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <sstream>

#include <ace/OS.h>

#include <so_5/rt/h/rt.hpp>
#include <so_5/api/h/api.hpp>

#include <so_5/disp/thread_pool/h/pub.hpp>

#include <so_5/h/spinlocks.hpp>

#include <test/so_5/time_limited_execution.hpp>

typedef std::set< so_5::current_thread_id_t > thread_id_set_t;

class thread_id_collector_t
	{
	public :
		void add_current_thread()
		{
			std::lock_guard< so_5::default_spinlock_t > l( m_lock );

			m_set.insert( so_5::query_current_thread_id() );
		}

		std::size_t set_size() const
		{
			return m_set.size();
		}

		const thread_id_set_t &
		query_set() const
		{
			return m_set;
		}

	private :
		so_5::default_spinlock_t m_lock;
		thread_id_set_t m_set;
	};

typedef std::shared_ptr< thread_id_collector_t > thread_id_collector_ptr_t;

typedef std::vector< thread_id_collector_ptr_t > collector_container_t;

struct msg_start : public so_5::rt::signal_t {};

struct msg_hello : public so_5::rt::signal_t {};

struct msg_shutdown : public so_5::rt::signal_t {};

class a_test_t : public so_5::rt::agent_t
{
	public:
		a_test_t(
			so_5::rt::so_environment_t & env,
			thread_id_collector_t & collector,
			const so_5::rt::mbox_ref_t & shutdowner_mbox,
			const so_5::rt::mbox_ref_t & starter_mbox )
			:	so_5::rt::agent_t( env )
			,	m_collector( collector )
			,	m_shutdowner_mbox( shutdowner_mbox )
			,	m_messages_sent( 0 )
		{
			so_subscribe( starter_mbox ).event(
					so_5::signal< msg_start >,
					[this]()
					{
						so_direct_mbox()->deliver_signal< msg_hello >();
					} );
		}

		virtual void
		so_define_agent()
		{
			so_subscribe( so_direct_mbox() ).event(
					so_5::signal< msg_hello >,
					&a_test_t::evt_hello );
		}

		void
		evt_hello()
		{
			m_collector.add_current_thread();

			if( ++m_messages_sent >= 10 )
				m_shutdowner_mbox->deliver_signal< msg_shutdown >();
			else
				so_direct_mbox()->deliver_signal< msg_hello >();
		}

	private :
		thread_id_collector_t & m_collector;
		const so_5::rt::mbox_ref_t m_shutdowner_mbox;

		std::size_t m_messages_sent;
};

class a_shutdowner_t : public so_5::rt::agent_t
{
	public :
		a_shutdowner_t(
			so_5::rt::so_environment_t & env,
			std::size_t working_agents )
			:	so_5::rt::agent_t( env )
			,	m_working_agents( working_agents )
		{}

		virtual void
		so_define_agent()
		{
			so_subscribe( so_direct_mbox() )
				.event( so_5::signal< msg_shutdown >,
					[this]() {
						--m_working_agents;
						if( !m_working_agents )
							so_environment().stop();
					} );
		}

	private :
		std::size_t m_working_agents;
};

const std::size_t cooperation_count = 2; // 1000;
const std::size_t cooperation_size = 2; // 100;
const std::size_t thread_count = 8;

collector_container_t
create_collectors()
{
	collector_container_t collectors;
	collectors.reserve( cooperation_count );
	for( std::size_t i = 0; i != cooperation_count; ++i )
		collectors.emplace_back( std::make_shared< thread_id_collector_t >() );

	return collectors;
}

void
run_sobjectizer( collector_container_t & collectors )
{
	so_5::api::run_so_environment(
		[&]( so_5::rt::so_environment_t & env )
		{
			so_5::rt::mbox_ref_t shutdowner_mbox;
			{
				auto c = env.create_coop( "shutdowner" );
				auto a = c->add_agent( new a_shutdowner_t( env,
						cooperation_count * cooperation_size ) );
				shutdowner_mbox = a->so_direct_mbox();
				env.register_coop( std::move( c ) );
			}

			auto starter_mbox = env.create_local_mbox();

			so_5::disp::thread_pool::params_t params;
			params.max_demands_at_once( 1024 );
			for( std::size_t i = 0; i != cooperation_count; ++i )
			{
				std::ostringstream ss;
				ss << "coop_" << i;

				auto c = env.create_coop( ss.str(),
						so_5::disp::thread_pool::create_disp_binder(
								"thread_pool", params ) );
				for( std::size_t a = 0; a != cooperation_size; ++a )
				{
					c->add_agent(
							new a_test_t(
									env,
									*(collectors[ i ]),
									shutdowner_mbox,
									starter_mbox ) );
				}
				env.register_coop( std::move( c ) );
			}

			starter_mbox->deliver_signal< msg_start >();
		},
		[]( so_5::rt::so_environment_params_t & params )
		{
			params.add_named_dispatcher(
					"thread_pool",
					so_5::disp::thread_pool::create_disp( thread_count ) );
		} );
}

void
analyze_results( const collector_container_t & collectors )
{
	thread_id_set_t all_threads;

	for( auto & c : collectors )
		if( 1 != c->set_size() )
		{
			std::ostringstream ss;
			ss << "there is a set with size: " << c->set_size();
			throw std::runtime_error( ss.str() );
		}
		else
			all_threads.insert( c->query_set().begin(), c->query_set().end() );

	std::cout << "all_threads size: " << all_threads.size() << std::endl;
}

void
run_and_check()
{
	auto collectors = create_collectors();

	run_sobjectizer( collectors );

	analyze_results( collectors );
}

int
main( int argc, char * argv[] )
{
	try
	{
		run_with_time_limit(
			[]()
			{
				run_and_check();
			},
			5,
			"cooperation_fifo test" );
	}
	catch( const std::exception & ex )
	{
		std::cerr << "Error: " << ex.what() << std::endl;
		return 1;
	}

	return 0;
}


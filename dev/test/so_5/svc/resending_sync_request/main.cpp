/*
 * A test for resending request parameter to another svc_handler.
 * wait_forever().sync_get() method chain is used.
 */

#include <iostream>
#include <exception>
#include <sstream>

#include <so_5/rt/h/rt.hpp>
#include <so_5/api/h/api.hpp>
#include <so_5/h/types.hpp>

#include <so_5/disp/active_obj/h/pub.hpp>

#include "../a_time_sentinel.hpp"

const std::size_t SVC_COUNT = 5;

struct msg_param : public so_5::rt::message_t
	{
		bool m_test_passed;
		mutable std::size_t m_svc_handled;

		msg_param()
			:	m_test_passed( false )
			,	m_svc_handled( 0 )
			{}
		~msg_param()
			{
				if( !m_test_passed )
					{
						std::cerr << "Destructor of msg_param called when "
								"test is not finished yet" << std::endl;

						std::abort();
					}

				if( m_svc_handled != SVC_COUNT )
					{
						std::cerr << "svc_handled value mismatch: actual="
								<< m_svc_handled
								<< ", expected="
								<< SVC_COUNT << std::endl;

						std::abort();
					}
			}
	};

class a_service_t
	:	public so_5::rt::agent_t
	{
	public :
		a_service_t(
			so_5::rt::so_environment_t & env,
			const so_5::rt::mbox_ref_t & self_mbox,
			const so_5::rt::mbox_ref_t & next_mbox,
			const msg_param * expected_msg_ptr )
			:	so_5::rt::agent_t( env )
			,	m_self_mbox( self_mbox )
			,	m_next_mbox( next_mbox )
			,	m_expected_msg_ptr( expected_msg_ptr )
			{
			}

		virtual so_5::rt::exception_reaction_t
		so_exception_reaction() const
			{
				return so_5::rt::abort_on_exception;
			}

		virtual void
		so_define_agent()
			{
				so_subscribe( m_self_mbox ).event( &a_service_t::svc );
			}

		void
		svc( const so_5::rt::event_data_t< msg_param > & evt )
			{
				if( m_expected_msg_ptr != evt.get() )
					{
						std::cerr << "Expected and actual pointers missmatch!"
								<< std::endl;

						std::abort();
					}
				evt->m_svc_handled++;

				if( m_next_mbox )
					{
						m_next_mbox->run_one().wait_forever().sync_get(
								evt.make_reference() );
					}
			}

	private :
		const so_5::rt::mbox_ref_t m_self_mbox;
		const so_5::rt::mbox_ref_t m_next_mbox;

		const msg_param * const m_expected_msg_ptr;
	};

class a_client_t
	:	public so_5::rt::agent_t
	{
	public :
		a_client_t(
			so_5::rt::so_environment_t & env,
			const so_5::rt::mbox_ref_t & svc_mbox,
			const so_5::rt::smart_atomic_reference_t< msg_param > & param )
			:	so_5::rt::agent_t( env )
			,	m_svc_mbox( svc_mbox )
			,	m_param( param )
			{}

		virtual so_5::rt::exception_reaction_t
		so_exception_reaction() const
			{
				return so_5::rt::abort_on_exception;
			}

		virtual void
		so_evt_start()
			{
				m_svc_mbox->run_one().wait_forever().sync_get( m_param );

				so_environment().stop();

				m_param->m_test_passed = true;
			}

	private :
		const so_5::rt::mbox_ref_t m_svc_mbox;

		so_5::rt::smart_atomic_reference_t< msg_param > m_param;
	};

void
init(
	so_5::rt::so_environment_t & env )
	{
		auto coop = env.create_coop(
				so_5::rt::nonempty_name_t( "test_coop" ),
				so_5::disp::active_obj::create_disp_binder( "active_obj" ) );

		so_5::rt::smart_atomic_reference_t< msg_param > msg( new msg_param() );

		auto svc_mbox = env.create_local_mbox();
		so_5::rt::mbox_ref_t current_svc_mbox = svc_mbox;
		so_5::rt::mbox_ref_t next_svc_mbox;

		for( std::size_t i = 0; i != SVC_COUNT; ++i ) 
			{
				if( i+1 != SVC_COUNT )
					next_svc_mbox = env.create_local_mbox();

				coop->add_agent(
						new a_service_t(
								env,
								current_svc_mbox,
								next_svc_mbox,
								msg.get() ) );

				current_svc_mbox = next_svc_mbox;
				next_svc_mbox = so_5::rt::mbox_ref_t();
			}

		coop->add_agent( new a_client_t( env, svc_mbox, msg ) );
		coop->add_agent( new a_time_sentinel_t( env ) );

		env.register_coop( std::move( coop ) );
	}

int
main( int, char ** )
	{
		try
			{
				so_5::api::run_so_environment(
					&init,
					std::move(
						so_5::rt::so_environment_params_t()
							.add_named_dispatcher(
								so_5::rt::nonempty_name_t( "active_obj" ),
								so_5::disp::active_obj::create_disp() ) ) );
			}
		catch( const std::exception & ex )
			{
				std::cerr << "Error: " << ex.what() << std::endl;
				return 1;
			}

		return 0;
	}


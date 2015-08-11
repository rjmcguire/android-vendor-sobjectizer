/*
 * A test of simple sequence of messages for prio::common_thread dispatcher.
 * A starting msg_hello is sent from on_start() action from the same coop.
 */

#include <so_5/all.hpp>

#include <various_helpers_1/time_limited_execution.hpp>

struct msg_hello : public so_5::rt::signal_t {};

void
define_receiver_agent(
	so_5::rt::agent_coop_t & coop,
	so_5::priority_t priority,
	const so_5::rt::mbox_t & common_mbox,
	std::string & sequence )
	{
		coop.define_agent( coop.make_agent_context() + priority )
			.event< msg_hello >(
				common_mbox,
				[priority, &sequence] {
					sequence += std::to_string(
						static_cast< std::size_t >( priority ) );
				} );
	}

std::string &
define_main_agent(
	so_5::rt::agent_coop_t & coop,
	const so_5::rt::mbox_t & common_mbox )
	{
		auto sequence = std::make_shared< std::string >();

		coop.define_agent( coop.make_agent_context() + so_5::prio::p0 )
			.on_start( [common_mbox] {
					so_5::send< msg_hello >( common_mbox );
				} )
			.event< msg_hello >(
				common_mbox,
				[&coop, sequence] {
					*sequence += "0";
					if( "76543210" != *sequence )
						throw std::runtime_error( "Unexpected value of sequence: " +
								*sequence );
					else
						coop.environment().stop();
				} );

		return *sequence;
	}

void
fill_coop(
	so_5::rt::agent_coop_t & coop )
	{
		using namespace so_5::prio;

		auto common_mbox = coop.environment().create_local_mbox();
		std::string & sequence = define_main_agent( coop, common_mbox );
		define_receiver_agent( coop, p1, common_mbox, sequence );
		define_receiver_agent( coop, p2, common_mbox, sequence );
		define_receiver_agent( coop, p3, common_mbox, sequence );
		define_receiver_agent( coop, p4, common_mbox, sequence );
		define_receiver_agent( coop, p5, common_mbox, sequence );
		define_receiver_agent( coop, p6, common_mbox, sequence );
		define_receiver_agent( coop, p7, common_mbox, sequence );
	}

int
main()
{
	try
	{
		// Do several iterations to increase probability of errors detection.
		std::cout << "runing iterations" << std::flush;
		for( int i = 0; i != 100; ++i )
		{
			run_with_time_limit(
				[]()
				{
					so_5::launch(
						[]( so_5::rt::environment_t & env )
						{
							using namespace so_5::disp::prio::common_thread;
							env.introduce_coop(
								create_private_disp( env )->binder(),
								fill_coop );
						} );
				},
				5,
				"simple sequence prio::common_thread dispatcher test" );
			std::cout << "." << std::flush;
		}
		std::cout << "done" << std::endl;
	}
	catch( const std::exception & ex )
	{
		std::cerr << "Error: " << ex.what() << std::endl;
		return 1;
	}

	return 0;
}


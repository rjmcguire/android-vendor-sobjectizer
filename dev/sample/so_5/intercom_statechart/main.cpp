/*
 * An example of implementation of hierarchical state machine by using
 * agent's states.
 */

#include <iostream>
#include <string>
#include <cctype>

// Main SObjectizer header file.
#include <so_5/all.hpp>

// Messages to be used for interaction with intercom agents.
struct key_cancel : public so_5::signal_t {};
struct key_bell : public so_5::signal_t {};
struct key_grid : public so_5::signal_t {};

struct key_digit
{
	char m_value;
};

// Private messages for intercom implementation.
namespace intercom_messages
{

struct activated : public so_5::signal_t {};
struct deactivate : public so_5::signal_t {};

struct display_text
{
	std::string m_what;
};

// Helper function for sending display_text message.
inline void show_on_display(
	const so_5::mbox_t & intercom_mbox,
	std::string what )
{
	so_5::send< display_text >( intercom_mbox, std::move(what) );
}

// Helper function for clearing display.
inline void clear_display(
	const so_5::mbox_t & intercom_mbox )
{
	so_5::send< display_text >( intercom_mbox, "" );
}

} // namespace intercom_messages

class inactivity_watcher final : public so_5::agent_t
{
	state_t inactive{ this, "inactive" };
	state_t active{ this, "active" };

	const std::chrono::seconds inactivity_time{ 10 };

public :

	inactivity_watcher(
		context_t ctx,
		so_5::mbox_t intercom_mbox )
		:	so_5::agent_t{ ctx }
		,	m_intercom_mbox{ std::move(intercom_mbox) }
	{
		inactive
			.on_enter( [this] { m_timer.release(); } )
			.event< intercom_messages::activated >(
					m_intercom_mbox, [this] { this >>= active; } );
		
		active
			.on_enter( [this] { reschedule_timer(); } )
			.event< key_cancel >( m_intercom_mbox, [this] { reschedule_timer(); } )
			.event< key_bell >( m_intercom_mbox, [this] { reschedule_timer(); } )
			.event< key_grid >( m_intercom_mbox, [this] { reschedule_timer(); } )
			.event(
					m_intercom_mbox, [this]( const key_digit & ) {
						reschedule_timer();
					} )
			.event< intercom_messages::deactivate >(
					m_intercom_mbox, [this] { this >>= inactive; } );

		this >>= inactive;
	}

private :
	const so_5::mbox_t m_intercom_mbox;
	so_5::timer_id_t m_timer;

	void reschedule_timer()
	{
		m_timer = so_5::send_periodic< intercom_messages::deactivate >(
				so_environment(),
				m_intercom_mbox,
				inactivity_time,
				std::chrono::seconds::zero() );
	}
};

class keyboard_lights final : public so_5::agent_t
{
	state_t off{ this, "off" };
	state_t on{ this, "on" };

public :
	keyboard_lights(
		context_t ctx,
		const so_5::mbox_t & intercom_mbox )
		:	so_5::agent_t{ ctx }
	{
		off
			.on_enter( []{ std::cout << "keyboard_lights OFF" << std::endl; } )
			.event< intercom_messages::activated >(
					intercom_mbox, [this]{ this >>= on; } );

		on
			.on_enter( []{ std::cout << "keyboard_lights ON" << std::endl; } )
			.event< intercom_messages::deactivate >(
					intercom_mbox, [this]{ this >>= off; } );

		this >>= off;
	}
};

class display final : public so_5::agent_t
{
	state_t off{ this, "off" };
	state_t on{ this, "on" };

public :
	display(
		context_t ctx,
		const so_5::mbox_t & intercom_mbox )
		:	so_5::agent_t{ ctx }
	{
		off
			.on_enter( []{ std::cout << "display OFF" << std::endl; } )
			.event< intercom_messages::activated >(
					intercom_mbox, [this]{ this >>= on; } );

		on
			.on_enter( []{ std::cout << "display ON" << std::endl; } )
			.event( intercom_mbox,
					[]( const intercom_messages::display_text & msg ) {
						std::cout << "display: '" << msg.m_what << "'" << std::endl;
					} )
			.event< intercom_messages::deactivate >(
					intercom_mbox, [this]{ this >>= off; } );

		this >>= off;
	}
};

class ringer final : public so_5::agent_t
{
	state_t
		off{ this, "off" },
		on{ this, "on" },
			ringing{ initial_substate_of{ on }, "ringing" },
			sleeping{ substate_of{ on }, "sleeping" };

	struct timer : public so_5::signal_t {};

public :
	struct dial_to { std::string m_number; };
	struct stop_dialing : public so_5::signal_t {};

	ringer(
		context_t ctx,
		so_5::mbox_t intercom_mbox )
		:	so_5::agent_t{ ctx }
		,	m_intercom_mbox{ std::move(intercom_mbox) }
	{
		this >>= off;

		off
			.on_enter( [this]{ m_timer.release(); } )
			.event( m_intercom_mbox, [this]( const dial_to & msg ) {
					m_number = msg.m_number;
					this >>= on;
				} );

		on
			.on_enter( [this] {
					m_timer = so_5::send_periodic< timer >(
							*this,
							std::chrono::milliseconds::zero(),
							std::chrono::milliseconds{ 1500 } );
				} )
			.on_exit( [this] {
					intercom_messages::clear_display( m_intercom_mbox );
				} )
			.event< stop_dialing >( m_intercom_mbox, [this]{ this >>= off; } );

		ringing
			.on_enter( [this]{ 
					intercom_messages::show_on_display(
							m_intercom_mbox, "RING" );
				} )
			.event< timer >( [this]{ this >>= sleeping; } );

		sleeping
			.on_enter( [this]{
					intercom_messages::show_on_display(
							m_intercom_mbox, m_number );
				} )
			.event< timer >( [this]{ this >>= ringing; } );
	}

private :
	const so_5::mbox_t m_intercom_mbox;

	so_5::timer_id_t m_timer;

	std::string m_number;
};

class controller final : public so_5::agent_t
{
	state_t
		inactive{ this, "inactive" },
		active{ this, "active" },

			wait_activity{
					initial_substate_of{ active }, "wait_activity" },
			number_selection{ substate_of{ active }, "number_selection" },
			dialling{ substate_of{ active }, "dialling" },

			special_code_selection{
					substate_of{ active }, "special_code_selection" },

				special_code_selection_0{
						initial_substate_of{ special_code_selection },
						"special_code_selection_0" },

				user_code_selection{
						substate_of{ special_code_selection },
						"user_code_selection" },
					user_code_apartment_number{
							initial_substate_of{ user_code_selection },
							"apartment_number" },
					user_code_secret{
							substate_of{ user_code_selection },
							"secret_code" },

				service_code_selection{
						substate_of{ special_code_selection },
						"service_code" },
				door_unlocked{
						substate_of{ special_code_selection },
						"door_unlocked" }
	;

	struct apartment_info
	{
		std::string m_number;
		std::string m_secret_key;

		apartment_info( std::string n, std::string k )
			:	m_number{ std::move(n) }, m_secret_key{ std::move(k) }
		{}
	};

	struct no_answer
	{
		int m_id;
	};

	struct lock_door
	{
		int m_id;
	};

public :
	controller(
		context_t ctx,
		so_5::mbox_t intercom_mbox )
		:	so_5::agent_t{ ctx }
		,	m_intercom_mbox{ std::move(intercom_mbox) }
		,	m_apartments{ make_apartment_info() }
	{
		inactive
			.transfer_to_state< key_digit >( m_intercom_mbox, active )
			.transfer_to_state< key_grid >( m_intercom_mbox, active )
			.transfer_to_state< key_bell >( m_intercom_mbox, active )
			.transfer_to_state< key_cancel >( m_intercom_mbox, active );

		active
			.on_enter( [this]{ active_on_enter(); } )
			.event< key_grid >(
					m_intercom_mbox,
					&controller::active_on_grid )
			.event< key_cancel >(
					m_intercom_mbox,
					&controller::active_on_cancel )
			.event< intercom_messages::deactivate >(
					m_intercom_mbox,
					[this]{ this >>= inactive; } );

		wait_activity
			.transfer_to_state< key_digit >( m_intercom_mbox, number_selection );

		number_selection
			.on_enter( [this]{ apartment_number_on_enter(); } )
			.event(
					m_intercom_mbox,
					&controller::apartment_number_on_digit )
			.event< key_bell >(
					m_intercom_mbox,
					&controller::apartment_number_on_bell )
			.event< key_grid >( m_intercom_mbox, []{} );

		dialling
			.on_enter( [this]{ dialling_on_enter(); } )
			.on_exit( [this]{ dialling_on_exit(); } )
			.event< key_grid >( m_intercom_mbox, []{} )
			.event< key_bell >( m_intercom_mbox, []{} )
			.event( m_intercom_mbox, []( const key_digit & ){} )
			.event( &controller::dialling_no_answer_from_apartment );

		special_code_selection_0
			.transfer_to_state< key_digit >( m_intercom_mbox, user_code_selection )
			.event< key_grid >(
					m_intercom_mbox,
					[this]{ this >>= service_code_selection; } );

		user_code_apartment_number
			.on_enter( [this]{ user_code_apartment_number_on_enter(); } )
			.event(
					m_intercom_mbox,
					&controller::apartment_number_on_digit )
			.event< key_grid >(
					m_intercom_mbox,
					[this]{ this >>= user_code_secret; } );

		user_code_secret
			.on_enter( [this]{ user_code_secret_on_enter(); } )
			.event(
					m_intercom_mbox,
					&controller::user_code_secret_on_digit )
			.event< key_bell >(
					m_intercom_mbox,
					&controller::user_code_secret_on_bell );

		service_code_selection
			.on_enter( [this]{ service_code_on_enter(); } )
			.event(
					m_intercom_mbox,
					&controller::service_code_on_digit )
			.event< key_grid >(
					m_intercom_mbox,
					&controller::service_code_on_grid );

		door_unlocked
			.on_enter( [this]{ door_unlocked_on_enter(); } )
			.on_exit( [this]{ door_unlocked_on_exit(); } )
			.event< key_grid >( m_intercom_mbox, []{} )
			.event< key_bell >( m_intercom_mbox, []{} )
			.event( m_intercom_mbox, []( const key_digit & ){} )
			.event( &controller::door_unlocked_on_lock_door );
	}

	virtual void so_evt_start() override
	{
		this >>= inactive;
	}

private :
	static const std::size_t max_apartment_number_size = 3u;
	static const std::size_t max_secret_code_size = 4u;
	static const std::size_t service_code_size = 5u;

	const so_5::mbox_t m_intercom_mbox;
	const std::vector< apartment_info > m_apartments;

	std::string m_apartment_number;
	int m_dial_id{ 0 };

	std::string m_user_secret_code;

	int m_lock_door_id{ 0 };

	std::string m_service_code;
	const std::string m_actual_service_code{ "12345" };

	static std::vector< apartment_info > make_apartment_info()
	{
		std::vector< apartment_info > result;
		result.reserve( 10 );

		result.emplace_back( "101", "1011" );
		result.emplace_back( "102", "1022" );
		result.emplace_back( "103", "1033" );
		result.emplace_back( "104", "1044" );
		result.emplace_back( "105", "1055" );
		result.emplace_back( "106", "1066" );
		result.emplace_back( "107", "1077" );
		result.emplace_back( "108", "1088" );
		result.emplace_back( "109", "1099" );
		result.emplace_back( "110", "1100" );

		return result;
	}

	void active_on_enter()
	{
		so_5::send< intercom_messages::activated >( m_intercom_mbox );
	}

	void active_on_cancel()
	{
		this >>= wait_activity;
	}

	void active_on_grid()
	{
		this >>= special_code_selection;
	}

	void apartment_number_on_enter()
	{
		m_apartment_number.clear();
	}

	void apartment_number_on_digit( const key_digit & msg )
	{
		if( m_apartment_number.size() < max_apartment_number_size )
			m_apartment_number += msg.m_value;

		intercom_messages::show_on_display(
				m_intercom_mbox, m_apartment_number );
	}

	void apartment_number_on_bell()
	{
		auto apartment = std::find_if( begin(m_apartments), end(m_apartments),
				[this]( const apartment_info & info ) {
					return info.m_number == m_apartment_number;
				} );

		if( apartment != end(m_apartments) )
			this >>= dialling;
		else
		{
			intercom_messages::show_on_display( m_intercom_mbox, "Err" );
			this >>= wait_activity;
		}
	}

	void dialling_on_enter()
	{
		++m_dial_id;
		so_5::send< ringer::dial_to >( m_intercom_mbox, m_apartment_number );
		so_5::send_delayed< no_answer >(
				*this, std::chrono::seconds{ 8 }, m_dial_id );
	}

	void dialling_on_exit()
	{
		so_5::send< ringer::stop_dialing >( m_intercom_mbox );
	}

	void dialling_no_answer_from_apartment( const no_answer & msg )
	{
		if( m_dial_id == msg.m_id )
		{
			intercom_messages::show_on_display( m_intercom_mbox, "No Answer" );
			this >>= wait_activity;
		}
	}

	void user_code_apartment_number_on_enter()
	{
		m_apartment_number.clear();
	}

	void user_code_secret_on_enter()
	{
		m_user_secret_code.clear();
		intercom_messages::clear_display( m_intercom_mbox );
	}

	void user_code_secret_on_digit( const key_digit & msg )
	{
		if( m_user_secret_code.size() < max_secret_code_size )
			m_user_secret_code += msg.m_value;

		intercom_messages::show_on_display(
				m_intercom_mbox, 
				std::string( m_user_secret_code.size(), '*' ) );
	}

	void user_code_secret_on_bell()
	{
		auto apartment = std::find_if( begin(m_apartments), end(m_apartments),
				[this]( const apartment_info & info ) {
					return info.m_number == m_apartment_number;
				} );

		if( apartment != end(m_apartments) &&
				m_user_secret_code == apartment->m_secret_key )
			this >>= door_unlocked;
		else
		{
			intercom_messages::show_on_display( m_intercom_mbox, "Err" );
			this >>= wait_activity;
		}
	}

	void door_unlocked_on_enter()
	{
		++m_lock_door_id;
		so_5::send_delayed< lock_door >(
				*this,
				std::chrono::seconds{ 5 },
				m_lock_door_id );
		intercom_messages::show_on_display( m_intercom_mbox, "unlocked" );
	}

	void door_unlocked_on_exit()
	{
		intercom_messages::clear_display( m_intercom_mbox );
	}

	void door_unlocked_on_lock_door( const lock_door & msg )
	{
		if( m_lock_door_id == msg.m_id )
			this >>= wait_activity;
	}

	void service_code_on_enter()
	{
		m_service_code.clear();
	}

	void service_code_on_digit( const key_digit & msg )
	{
		if( m_service_code.size() < service_code_size )
			m_service_code += msg.m_value;

		intercom_messages::show_on_display(
				m_intercom_mbox, 
				std::string( m_service_code.size(), '#' ) );
	}

	void service_code_on_grid()
	{
		if( !m_service_code.empty() )
		{
			if( m_service_code == m_actual_service_code )
				this >>= door_unlocked;
			else
			{
				intercom_messages::show_on_display( m_intercom_mbox, "Err" );
				this >>= wait_activity;
			}
		}
	}
};

so_5::mbox_t create_intercom( so_5::environment_t & env )
{
	so_5::mbox_t intercom_mbox;
	env.introduce_coop( [&]( so_5::coop_t & coop ) {
		intercom_mbox = env.create_mbox();

		coop.make_agent< controller >( intercom_mbox );
		coop.make_agent< inactivity_watcher >( intercom_mbox );
		coop.make_agent< keyboard_lights >( intercom_mbox );
		coop.make_agent< display >( intercom_mbox );
		coop.make_agent< ringer >( intercom_mbox );
	} );

	return intercom_mbox;
}

void demo()
{
	// A SObjectizer instance.
	so_5::wrapped_env_t sobj{
		[]( so_5::environment_t & ) {},
		[]( so_5::environment_params_t & params ) {
//			params.message_delivery_tracer( so_5::msg_tracing::std_clog_tracer() );
		} };

	auto intercom = create_intercom( sobj.environment() );

	while( true )
	{
		std::cout << "enter digit or 'c' or 'b' or '#' (or 'exit' to stop): "
			<< std::flush;

		std::string choice;
		std::cin >> choice;

		if( "c" == choice ) so_5::send< key_cancel >( intercom );
		else if( "b" == choice ) so_5::send< key_bell >( intercom );
		else if( "#" == choice ) so_5::send< key_grid >( intercom );
		else if( "exit" == choice ) break;
		else if( 1 == choice.size() && std::isdigit( choice[ 0 ] ) )
			so_5::send< key_digit >( intercom, choice[ 0 ] );
	}

	// SObjectizer will be stopped automatically.
}

int main()
{
	try
	{
		demo();
	}
	catch( const std::exception & ex )
	{
		std::cerr << "Error: " << ex.what() << std::endl;
	}

	return 0;
}


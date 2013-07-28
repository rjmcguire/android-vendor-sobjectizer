/*
	SObjectizer 5.
*/

/*!
	\file
	\brief ����� ������ �� message.
*/

#if !defined( _SO_5__RT__MESSAGE_REF_HPP_ )
#define _SO_5__RT__MESSAGE_REF_HPP_

#include <so_5/h/declspec.hpp>

namespace so_5
{

namespace rt
{

class message_t;

//! ����� ����� ������ �� message_t.
class SO_5_TYPE message_ref_t
{
	public:
		message_ref_t();

		explicit message_ref_t(
			message_t * message );

		message_ref_t(
			const message_ref_t & message_ref );

		void
		operator = ( const message_ref_t & message_ref );

		~message_ref_t();

		inline const message_t *
		get() const
		{
			return m_message_ptr;
		}

		inline message_t *
		get()
		{
			return m_message_ptr;
		}

		inline const message_t *
		operator -> () const
		{
			return m_message_ptr;
		}

		inline message_t *
		operator -> ()
		{
			return m_message_ptr;
		}

		inline message_t &
		operator * ()
		{
			return *m_message_ptr;
		}

		inline const message_t &
		operator * () const
		{
			return *m_message_ptr;
		}

	private:
		//! ��������� ���������� ������ �� message
		//! � � ������ ������������� ������� ���.
		void
		inc_message_ref_count();

		//! ��������� ���������� ������ �� message
		//! � � ������ ������������� ������� ���.
		void
		dec_message_ref_count();

		message_t * m_message_ptr;
};

} /* namespace rt */

} /* namespace so_5 */

#endif

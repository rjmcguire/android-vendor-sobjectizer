require 'mxx_ru/binary_unittest'

Mxx_ru::setup_target(
	Mxx_ru::Binary_unittest_target.new(
		"test/so_5/coop/throw_on_define_agent/prj.ut.rb",
		"test/so_5/coop/throw_on_define_agent/prj.rb" )
)

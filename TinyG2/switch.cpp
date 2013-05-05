/*
 * switch.cpp - switch handling functions
 * This file is part of the TinyG project
 *
 * Copyright (c) 2013 Alden S. Hart Jr.
 * Copyright (c) 2013 Robert Giseburt
 *
 * This file ("the software") is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as published by the
 * Free Software Foundation. You should have received a copy of the GNU General Public
 * License, version 2 along with the software.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, you may use this file as part of a software library without
 * restriction. Specifically, if other files instantiate templates or use macros or
 * inline functions from this file, or you compile this file and link it with  other
 * files to produce an executable, this file does not by itself cause the resulting
 * executable to be covered by the GNU General Public License. This exception does not
 * however invalidate any other reasons why the executable file might be covered by the
 * GNU General Public License.
 *
 * THE SOFTWARE IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY
 * WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
 * SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/* Switch Modes
 *
 *	The switches are considered to be homing switches when machine_state is
 *	MACHINE_HOMING. At all other times they are treated as limit switches:
 *	  - Hitting a homing switch puts the current move into feedhold
 *	  - Hitting a limit switch causes the machine to shut down and go into lockdown until reset
 *
 * 	The normally open switch modes (NO) trigger an interrupt on the falling edge 
 *	and lockout subsequent interrupts for the defined lockout period. This approach 
 *	beats doing debouncing as an integration as switches fire immediately.
 *
 * 	The normally closed switch modes (NC) trigger an interrupt on the rising edge 
 *	and lockout subsequent interrupts for the defined lockout period. Ditto on the method.
 */

#include "tinyg2.h"
#include "switch.h"
#include "hardware.h"
#include "canonical_machine.h"

// Allocate switch array structure
switches_t sw;

static void _do_feedhold(switch_t *s);

/*
 * switch_init() - initialize homing/limit switches
 *
 *	This function assumes all Motate pins have been set up and that 
 *	SW_PAIRS and SW_POSITIONS is accurate
 *
 *	Note: `type` and `mode` are not initialized as they should be set from configuration
 */
static void _no_action(switch_t *s) { return; }

void switch_init(void)
{
//	sw.type = SW_NORMALLY_OPEN;						// set from config
//	sw.edge_flag = 0;
//	sw.edge_pair = 0;
//	sw.edge_position = 0;

	switch_t *s;
	
	for (uint8_t axis=0; axis<SW_PAIRS; axis++) {
		for (uint8_t position=0; position<SW_POSITIONS; position++) {
			s = &sw.s[axis][position];
			
			s->type = sw.type;				// propagate type from global type
//			s->mode = SW_MODE_DISABLED;		// set from config			
			s->state = false;
			s->edge = SW_NO_EDGE;
			s->debounce_ticks = SW_LOCKOUT_TICKS;
			s->debounce_timeout = 0;

			// functions bound to each switch
			s->when_open = _no_action;
			s->when_closed = _no_action;
			s->on_leading = _do_feedhold;
//			s->on_trailing = _do_feedhold;		
			s->on_trailing = _no_action;		
		}
	}
	// functions bound ti individual switches
	// <none>
}

/*
 * poll_switches() - run a polling cycle on all switches
 */
uint8_t poll_switches()
{
	read_switch(&sw.s[AXIS_X][SW_MIN], axis_X_min_pin);
	read_switch(&sw.s[AXIS_X][SW_MAX], axis_X_max_pin);
	read_switch(&sw.s[AXIS_Y][SW_MIN], axis_Y_min_pin);
	read_switch(&sw.s[AXIS_Y][SW_MAX], axis_Y_max_pin);
	read_switch(&sw.s[AXIS_Z][SW_MIN], axis_Z_min_pin);
	read_switch(&sw.s[AXIS_Z][SW_MAX], axis_Z_max_pin);
	read_switch(&sw.s[AXIS_A][SW_MIN], axis_A_min_pin);
	read_switch(&sw.s[AXIS_A][SW_MAX], axis_A_max_pin);
	read_switch(&sw.s[AXIS_B][SW_MIN], axis_B_min_pin);
	read_switch(&sw.s[AXIS_B][SW_MAX], axis_B_max_pin);
	read_switch(&sw.s[AXIS_C][SW_MIN], axis_C_min_pin);
	read_switch(&sw.s[AXIS_C][SW_MAX], axis_C_max_pin);
	return (false);
}

/*
 * read_switch() - read switch with NO/NC, debouncing and edge detection
 *
 *	Returns true if switch state changed - e.g. leading or falling edge detected
 *	Assumes pin_value input = 1 means open, 0 is closed. Pin sense is adjusted to mean:
 *	  0 = open for both NO and NC switches
 *	  1 = closed for both NO and NC switches
 */
uint8_t read_switch(switch_t *s, uint8_t pin_value)
{
	// return if switch is not enabled
	if (s->mode == SW_MODE_DISABLED) { return (false); }

	// return if no change in state
	uint8_t pin_sense_corrected = (pin_value ^ (s->type ^ 1));	// correct for NO or NC mode
  	if (pin_sense_corrected == s->state) { 
		// process switch actions
		if (s->state == SW_OPEN) { s->when_open(s); }
		if (s->state == SW_CLOSED) { s->when_closed(s); }
		return (false);
	}

	// switch is in debounce lockout interval
	if ((s->debounce_timeout != 0) && (s->debounce_timeout > GetTickCount())) { return (false);} 

	// switch changed state
	s->state = pin_sense_corrected;
	s->debounce_timeout = (GetTickCount() + s->debounce_ticks);

	// process edge switch actions
	s->edge = ((s->state == SW_OPEN) ? SW_TRAILING : SW_LEADING);
	if (s->edge == SW_LEADING) { s->on_leading(s); }
	if (s->edge == SW_TRAILING) { s->on_trailing(s); }
	return (true);
}

static void _do_feedhold(switch_t *s) 
{ 
	IndicatorLed.toggle();
	if (cm.cycle_state == CYCLE_HOMING) {		// regardless of switch type
		cm.request_feedhold = true;
		} else if (s->mode & SW_LIMIT_BIT) {		// set flag if it's a limit switch
		cm.limit_flag = true;
	}
	return; 
}


/*
 * switch_get_switch_mode() 	- return switch mode setting
 * switch_get_limit_thrown()  - return true if a limit was tripped
 * switch_get_sw_num()  		- return switch number most recently thrown
 */

uint8_t get_switch_mode(uint8_t sw_num) { return (0);}	// ++++

//###########################################################################
//##### UNIT TESTS ##########################################################
//###########################################################################

#ifdef __UNIT_TESTS
#ifdef __UNIT_TEST_GPIO

void switch_unit_tests()
{
//	_isr_helper(SW_MIN_X, X);
	while (true) {
		switch_led_toggle(1);
	}
}

#endif // __UNIT_TEST_GPIO
#endif
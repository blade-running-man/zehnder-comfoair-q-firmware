// The inputs the two halves of this suite share: the healthy device, the product of all inputs,
// and the compile-time guard that keeps that product whole.
//
// A HEADER INSIDE THE SUITE DIRECTORY AND NOT UNDER include/, deliberately. include/ in this tree
// means a component's public contract, and this is a fixture -- it exists to be edited alongside
// the cases that use it. The same argument platformio.ini makes about the fake IDF headers under
// test/test_can_transport/idf applies here: a fixture promoted to a shared place is a fixture that
// grows a feature for one suite and quietly weakens another. Nothing outside test/test_phase/ may
// include this.
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "comfoair_phase.h"

// The healthy device: every start branch of app_main returned OK, the network is up, the bus
// talks, the broker is configured and connected. Every case below spoils exactly one field, so
// the field a case is about is the field it names.
inline comfoair_phase_inputs_t online(void) {
  comfoair_phase_inputs_t in{};
  in.machine_ready       = true;
  in.can_started         = true;
  in.poll_started        = true;
  in.control_started     = true;
  in.net_started         = true;
  in.mqtt_ready          = true;
  in.mqtt_client_started = true;
  in.http_started        = true;
  in.prov_state          = COMFOAIR_PROV_CONNECTED;
  in.prov_failure        = COMFOAIR_PROV_FAIL_NONE;
  in.bus_started         = true;
  in.broker_configured   = true;
  in.broker_connected    = true;
  return in;
}

// Converts to whatever an aggregate's member happens to be. Declared, never defined: it lives only
// inside decltype, where it lets the compiler answer "how many initialisers does this struct
// take?" -- the only way to count fields before reflection.
struct fills_any_field {
  template <class T>
  constexpr operator T() const noexcept;
};
template <std::size_t>
using field_slot = fills_any_field;

template <class T, std::size_t... I>
constexpr auto takes_this_many(std::index_sequence<I...>, int)
    -> decltype(T{field_slot<I>{}...}, true) {
  return true;
}
template <class T, std::size_t... I>
constexpr bool takes_this_many(std::index_sequence<I...>, long) {
  return false;
}
template <class T, std::size_t N>
constexpr bool has_exactly_n_fields = takes_this_many<T>(std::make_index_sequence<N>{}, 0) &&
                                      !takes_this_many<T>(std::make_index_sequence<N + 1>{}, 0);

// The fifteen booleans of comfoair_phase_inputs_t, as one bit each, so that the sweeps below are
// a product and not a hand-picked sample.
inline constexpr int INPUT_BITS  = 15;
inline constexpr int INPUT_ENUMS = 2;   // prov_state and prov_failure, which are swept by name

// THE COUNT IS THE POINT. A sixteenth flag changes neither sizeof (five bytes of this layout are
// padding) nor this line, and combination() would then produce 2^15 of the 2^16 inputs while both
// sweeps went on calling themselves the whole product. The decomposition below makes such a field
// noisy -- the compiler demands a name -- but only this line makes it fail.
static_assert(has_exactly_n_fields<comfoair_phase_inputs_t, INPUT_BITS + INPUT_ENUMS>,
              "comfoair_phase_inputs_t gained or lost a field: INPUT_BITS and the shifts in "
              "combination() no longer cover it, and the sweeps are a sample calling itself "
              "a product");

inline comfoair_phase_inputs_t combination(unsigned bits, comfoair_prov_state_t state,
                                           comfoair_prov_failure_t failure) {
  comfoair_phase_inputs_t in{};
  in.machine_ready       = (bits >> 0) & 1u;
  in.ota_in_progress     = (bits >> 1) & 1u;
  in.can_started         = (bits >> 2) & 1u;
  in.poll_started        = (bits >> 3) & 1u;
  in.control_started     = (bits >> 4) & 1u;
  in.net_started         = (bits >> 5) & 1u;
  in.mqtt_ready          = (bits >> 6) & 1u;
  in.mqtt_client_started = (bits >> 7) & 1u;
  in.http_started        = (bits >> 8) & 1u;
  in.ap_on_air           = (bits >> 9) & 1u;
  in.bus_started         = (bits >> 10) & 1u;
  in.bus_silent          = (bits >> 11) & 1u;
  in.broker_configured   = (bits >> 12) & 1u;
  in.broker_connected    = (bits >> 13) & 1u;
  in.broker_rejected     = (bits >> 14) & 1u;
  in.prov_state          = state;
  in.prov_failure        = failure;
  return in;
}

inline constexpr comfoair_prov_state_t ALL_STATES[] = {
    COMFOAIR_PROV_UNCONFIGURED, COMFOAIR_PROV_CONNECTING,  COMFOAIR_PROV_CONNECTED,
    COMFOAIR_PROV_TRIAL,        COMFOAIR_PROV_RETRYING,    COMFOAIR_PROV_FALLBACK_AP,
    COMFOAIR_PROV_WINDOW_CLOSED,
};
static_assert(sizeof(ALL_STATES) / sizeof(ALL_STATES[0]) == COMFOAIR_PROV_STATE_COUNT,
              "the sweeps say 'all seven provisioning states' and this list no longer is all of "
              "them: an unlisted state is one the product never reaches");

inline constexpr comfoair_prov_failure_t ALL_FAILURES[] = {
    COMFOAIR_PROV_FAIL_NONE,          COMFOAIR_PROV_FAIL_NETWORK_ABSENT,
    COMFOAIR_PROV_FAIL_WRONG_PASSWORD, COMFOAIR_PROV_FAIL_REFUSED,
    COMFOAIR_PROV_FAIL_NO_ADDRESS,
};
static_assert(sizeof(ALL_FAILURES) / sizeof(ALL_FAILURES[0]) == COMFOAIR_PROV_FAIL_COUNT,
              "a Wi-Fi verdict is missing from the sweep, so no case here has ever seen it");

// THE INPUT SPACE IS GUARDED IN THREE PLACES AND NEEDS ALL THREE. The arity assertion above says
// the struct has no field the bits miss; the two assertions here say the enumerations have no
// value the lists miss; and test_every_bit_of_the_sweep_moves_a_flag_of_its_own says the shifts
// below actually reach the fields they claim. Each was added because the other two let a real
// mutation through green.


//
// metamod - shared test support: process-wide globals and an engine mock
//

#ifndef MMFG_TEST_STUBS_H
#define MMFG_TEST_STUBS_H

// Reset the engine mock, the game DLL description and every global the
// production sources read, so each test case starts from a known state.
void mm_test_reset(void);

// Engine output captured through the mocked engine function table.
int         mm_test_alert_count(void);
const char* mm_test_alert_msg(int index);

// Game directory reported by the mocked GET_GAME_DIR.
void mm_test_set_gamedir(const char* dir);

#endif // MMFG_TEST_STUBS_H

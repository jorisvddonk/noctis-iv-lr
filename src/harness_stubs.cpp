/*
	HARNESS_STUBS.CPP - linker stubs for the nivtest harness.

	The harness links the generation modules NOCTIS-0.CPP / NOCTIS-1.CPP
	(plus Brtl.cpp) but deliberately does NOT link NOCTIS.CPP (the game main,
	full of raylib/graphics/input code we neither want nor can run headless).

	NOCTIS-0/1 reference a handful of symbols that are *defined* in
	NOCTIS.CPP. Those become unresolved externals once NOCTIS.OBJ is omitted.
	Every one of them lives only in code paths the harness never executes
	(HUD label rendering, the off-screen framebuffer swap), so providing inert
	definitions here is safe and does NOT alter generation behaviour.
*/

#include <cstdint>

/* data symbols defined in NOCTIS.CPP that N0/N1 reference */
int32_t star_label_pos   = -1;
int8_t  star_label[25]   = "UNKNOWN STAR / CLASS ...";
int32_t planet_label_pos = -1;
int8_t  planet_label[25] = "NAMELESS PLANET / N. ...";

/* function defined in NOCTIS.CPP that N1 references (framebuffer swap). */
void swapBuffers()
{
	/* never called by the generation code paths under test */
}

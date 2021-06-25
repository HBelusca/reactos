/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Direct Beep Music in the Kernel.
 * COPYRIGHT:   Copyright 2021 Hermes Belusca-Maito
 */

/* 12-tone chromatic scale, at the 4-th octave, A440Hz equal temperament */
#define Am  415
#define A   440
#define As  466

#define Bm  As
#define B   494
#define Bs  C

#define Cm  B
#define C   523
#define Cs  554

#define Dm  Cs
#define D   587
#define Ds  622

#define Em  Ds
#define E   659
#define Es  F

#define Fm  E
#define F   698
#define Fs  740

#define Gm  Fs
#define G   784
#define Gs  831


/** @brief  Stop beeping. **/
#define STOP()  HalMakeBeep(0)

/**
 * @brief   Emits a note on a given octave.
 **/
#define BEEP_(note, oct)    HalMakeBeep(note << (oct-4))

/**
 * @brief   Emits a note on a given octave, halts for a
 *          duration (in milliseconds) but doesn't stop after.
 **/
#define BEEP(note, oct, duration) \
do { \
    BEEP_(note, oct); \
    KeStallExecutionProcessor(duration * 1000UL); \
} while (0)

/**
 * @brief   Emits a note on a given octave, halts for
 *          a duration (in milliseconds) then stops.
 **/
#define BEEP_STOP(note, oct, duration) \
do { \
    BEEP(note, oct, duration); \
    STOP(); \
    /* Add an extra short 10ms stop delay */ \
    KeStallExecutionProcessor(10 * 1000UL);  \
} while (0)

/* EOF */

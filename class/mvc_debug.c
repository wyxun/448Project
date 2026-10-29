/**
 * @file    mvc_debug.c
 * @brief   MShell commands used for board bring-up and diagnostics.
 * @author  Modus project
 * @date    2026-10-28
 */

#include "mvc.h"
#include "mdi/instance.h"
#include "mdebug/mshell.h"
#include "mdebug/util_debug.h"

#if MSHELL_ENABLE
extern mvc_t tMvc;
extern mircoelec_t tMircoelec;

/**
 * @brief Print current input readings and consume pending key clicks.
 * @param pchArgs Unused shell arguments.
 * @return None.
 */
static void _debug_InputCommand(const char *pchArgs)
{
    input_driver_temperature_t tTemperature = {0};
    input_driver_result_t eResult = INPUT_DRIVER_RESULT_NO_DATA;
    uint8_t chKeys = 0U;
    unsigned i = 0U;

    (void)pchArgs;
    for (i = 0U; i < INPUT_DRIVER_NTC_COUNT; ++i) {
        tTemperature.nDeciC = 0;
        tTemperature.eResult = INPUT_DRIVER_RESULT_NO_DATA;
        eResult = input_driver_GetTemperature(&tMvc.tInput,
            (input_driver_ntc_t)i, &tTemperature);
        MLOGF(I, "NTC%u=%d (0.1C) status=%d\r\n",
              i + 1U, (int)tTemperature.nDeciC, (int)eResult);
    }
    eResult = input_driver_GetKeyValue(&tMvc.tInput, &chKeys);
    MLOGF(I, "keys=%u status=%d\r\n", (unsigned)chKeys, (int)eResult);
}

MODUS_SHELL_CMD(input, _debug_InputCommand,
                "Read temperatures and consume pending key clicks");

/**
 * @brief Echo one shell command argument to the configured debug output.
 * @param pchArgs Text received after the echo command.
 * @return None.
 */
static void _debug_EchoCommand(const char *pchArgs)
{
    MLOGF(I, "echo: %s\r\n", pchArgs);
}

MODUS_SHELL_CMD(echo, _debug_EchoCommand,
                "Echo text to verify serial RX/TX");

/**
 * @brief Enable or disable the bio supply relay output.
 * @param pchArgs Text received after the bio_power command.
 * @return None.
 */
static void _debug_BioPowerCommand(const char *pchArgs)
{
    const char *pchText = pchArgs;
    bool bEnable = false;
    uint32_t wPinLevel = 0U;

    if ((mshell_Parse(&pchText, &bEnable) != MSHELL_PARSE_OK)
        || !mshell_AtEnd(pchText)) {
        MLOGF(W, "Usage: bio_power <on|off>\r\n");
        return;
    }

    wPinLevel = bEnable ? 1U : 0U;
    MDI_IO_Write(bio_power, wPinLevel);
    MLOGF(I, "BIO power %s (PA11=%lu)\r\n",
          bEnable ? "enabled" : "disabled",
          (unsigned long)wPinLevel);
}

MODUS_SHELL_CMD(bio_power, _debug_BioPowerCommand,
                "Control bio power: on | off");

/**
 * @brief Enable or disable the BIO heater control.
 * @param pchArgs Text received after the heat command.
 * @return None.
 */
static void _debug_HeatCommand(const char *pchArgs)
{
    const char *pchText = pchArgs;

    if (mshell_ConsumeWord(&pchText, "start") && mshell_AtEnd(pchText)) {
        if ((tMvc.eState != MVC_STATE_READY)
            || (tMircoelec.eState != MIRCROELEC_STATE_READY)) {
            MLOGF(W, "Heat start failed: input or BIO not ready\r\n");
            return;
        }
        heater_control_Start(&tMircoelec.tHeater);
        MLOGF(I, "Heat control started\r\n");
        return;
    }
    pchText = pchArgs;
    if (mshell_ConsumeWord(&pchText, "stop") && mshell_AtEnd(pchText)) {
        heater_control_Stop(&tMircoelec.tHeater);
        MLOGF(I, "Heat control stopped\r\n");
        return;
    }
    MLOGF(W, "Usage: heat start | heat stop\r\n");
}

MODUS_SHELL_CMD(heat, _debug_HeatCommand, "Heater control: start | stop");

/** @brief Exercise the normal RF command path using the shared parameter bank. */
static void _debug_RfProbe(mvc_probe_t eProbe, const char *pchText)
{
    probe_command_type_t eCommand = MVC_PROBE_COMMAND_NONE;
    uint16_t hwEnergy = 0U;
    if (mshell_ConsumeWord(&pchText, "stop") && mshell_AtEnd(pchText)) {
        eCommand = MVC_PROBE_COMMAND_STOP;
    } else {
        if (mshell_ConsumeWord(&pchText, "start")) {
            eCommand = MVC_PROBE_COMMAND_START;
        } else if (mshell_ConsumeWord(&pchText, "energy")) {
            eCommand = MVC_PROBE_COMMAND_ENERGY;
        } else {
            MLOGF(W, "Usage: probe cap|res start|energy <0..1000> | stop\r\n");
            return;
        }
        if ((mshell_Parse(&pchText, &hwEnergy) != MSHELL_PARSE_OK)
            || !mshell_AtEnd(pchText) || (hwEnergy > 1000U)) {
            MLOGF(W, "RF energy must be 0..1000\r\n");
            return;
        }
        if (eProbe == MVC_PROBE_CAP) {
            tMvc.tProbeParam.tCap.hwEnergyPermille = hwEnergy;
        } else {
            tMvc.tProbeParam.tRes.hwEnergyPermille = hwEnergy;
        }
    }
    if (!mvc_ProbeCommand(&tMvc, eProbe, eCommand)) {
        MLOGF(W, "RF command rejected\r\n");
    } else {
        MLOGF(I, "RF command queued\r\n");
    }
}

/** @brief Control Client RF448 through the normal MVC and protocol path.
 * @param pchArgs cap|res followed by start/energy <0..1000> or stop.
 * @return None; local acceptance and Client replies are logged separately.
 */
static void _debug_Rf448Command(const char *pchArgs)
{
    if (mshell_ConsumeWord(&pchArgs, "cap")) {
        _debug_RfProbe(MVC_PROBE_CAP, pchArgs);
    } else if (mshell_ConsumeWord(&pchArgs, "res")) {
        _debug_RfProbe(MVC_PROBE_RES, pchArgs);
    } else {
        MLOGF(W, "Usage: rf448 cap|res start|energy <0..1000> | stop\r\n");
    }
}

MODUS_SHELL_CMD(rf448, _debug_Rf448Command,
    "RF448: cap|res start|energy <0..1000> | stop");

/** @brief Set the BIO parameter bank or submit a simulated probe key.
 * @param pchArgs MShell command arguments.
 * @return None.
 */
static void _debug_ProbeCommand(const char *pchArgs)
{
    const char *pchText = pchArgs;
    mircoelec_param_t tBioParam = {0};
    uint32_t wAHz = 0U;
    uint32_t wBHz = 0U;
    uint32_t wAmplitudePercent = 0U;

    if (mshell_ConsumeWord(&pchText, "cap")) {
        _debug_RfProbe(MVC_PROBE_CAP, pchText);
        return;
    }
    if (mshell_ConsumeWord(&pchText, "res")) {
        _debug_RfProbe(MVC_PROBE_RES, pchText);
        return;
    }
    if (!mshell_ConsumeWord(&pchText, "bio")) {
        MLOGF(W,
            "Usage: probe bio start <sin|dig> <A_Hz> <B_Hz> <B_amp%%> "
            "| stop\r\n");
        return;
    }

    if (mshell_ConsumeWord(&pchText, "stop")) {
        if (!mshell_AtEnd(pchText)
            || !mvc_ProbeCommand(&tMvc, MVC_PROBE_BIO,
                                 MVC_PROBE_COMMAND_STOP)) {
            MLOGF(W, "BIO probe is not active\r\n");
            return;
        }
        MLOGF(I, "BIO stop command queued\r\n");
        return;
    }

    if (!mshell_ConsumeWord(&pchText, "start")) {
        MLOGF(W,
            "Usage: probe bio start <sin|dig> <A_Hz> <B_Hz> <B_amp%%> "
            "| stop\r\n");
        return;
    }

    if (mshell_ConsumeWord(&pchText, "sin")) {
        tBioParam.eWaveType = DAC_DRIVER_WAVE_SINE;
    } else if (mshell_ConsumeWord(&pchText, "dig")) {
        tBioParam.eWaveType = DAC_DRIVER_WAVE_DIGITAL;
    } else {
        MLOGF(W,
            "Usage: probe bio start <sin|dig> <A_Hz> <B_Hz> <B_amp%%> "
            "| stop\r\n");
        return;
    }

    if ((mshell_Parse(&pchText, &wAHz) != MSHELL_PARSE_OK)
        || (mshell_Parse(&pchText, &wBHz) != MSHELL_PARSE_OK)
        || (mshell_Parse(&pchText, &wAmplitudePercent) != MSHELL_PARSE_OK)
        || !mshell_AtEnd(pchText)
        || (wAHz == 0U) || (wAHz > DAC_DRIVER_MAX_FREQUENCY_HZ)
        || (wBHz == 0U) || (wBHz > DAC_DRIVER_MAX_FREQUENCY_HZ)
        || (wAmplitudePercent > DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT)) {
        MLOGF(W,
            "Usage: probe bio start <sin|dig> <A_Hz> <B_Hz> <B_amp%%> "
            "| stop\r\n");
        return;
    }

    if ((tMvc.eState != MVC_STATE_READY)
        || (tMvc.eActiveProbe != MVC_PROBE_NONE)
        || (tMvc.tProbeCommand.eCommand != MVC_PROBE_COMMAND_NONE)) {
        MLOGF(W, "BIO probe is busy or not ready\r\n");
        return;
    }

    tBioParam.wAHz = wAHz;
    tBioParam.wBHz = wBHz;
    tBioParam.chAmplitudePercent = (uint8_t)wAmplitudePercent;
    tMvc.tProbeParam.tBio = tBioParam;
    if (!mvc_ProbeCommand(&tMvc, MVC_PROBE_BIO, MVC_PROBE_COMMAND_START)) {
        MLOGF(W, "BIO start command rejected\r\n");
        return;
    }
    MLOGF(I, "BIO start queued: A=%lu Hz B=%lu Hz amp=%lu%%\r\n",
          (unsigned long)wAHz, (unsigned long)wBHz,
          (unsigned long)wAmplitudePercent);
}

MODUS_SHELL_CMD(probe, _debug_ProbeCommand,
                "BIO start <sin|dig> <A> <B> <amp>; cap|res start|energy <0..1000>; stop");

/**
 * @brief Parse and execute one DAC waveform command.
 * @param pchArgs Text after the dac command name.
 * @return None.
 */
static void _debug_DacCommand(const char *pchArgs)
{
    const char *pchText = pchArgs;
    uint32_t wAHz = 0U;
    uint32_t wBHz = 0U;
    uint32_t wBAmplitudePercent = DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT;
    dac_driver_result_t eResult = DAC_DRIVER_RESULT_OK;

    if (mshell_ConsumeWord(&pchText, "start")) {
        dac_driver_wave_type_t eWaveType = DAC_DRIVER_WAVE_SINE;

        if (mshell_ConsumeWord(&pchText, "dig")) {
            eWaveType = DAC_DRIVER_WAVE_DIGITAL;
        } else if (mshell_ConsumeWord(&pchText, "sin")) {
            eWaveType = DAC_DRIVER_WAVE_SINE;
        }
        if ((mshell_Parse(&pchText, &wAHz) != MSHELL_PARSE_OK)
            || (mshell_Parse(&pchText, &wBHz) != MSHELL_PARSE_OK)
            || (wAHz > DAC_DRIVER_MAX_FREQUENCY_HZ)
            || (wBHz > DAC_DRIVER_MAX_FREQUENCY_HZ)) {
            MLOGF(W,
                "Usage: dac start [dig|sin] <A_Hz> <B_Hz> [B_percent]\r\n");
            return;
        }
        if (!mshell_AtEnd(pchText)
            && (mshell_Parse(&pchText, &wBAmplitudePercent)
                != MSHELL_PARSE_OK)) {
            MLOGF(W,
                "Usage: dac start [dig|sin] <A_Hz> <B_Hz> [B_percent]\r\n");
            return;
        }
        if (!mshell_AtEnd(pchText)
            || (wBAmplitudePercent
                > DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT)) {
            MLOGF(W,
                "Usage: dac start [dig|sin] <A_Hz> <B_Hz> [B_percent]\r\n");
            return;
        }
        eResult = mircoelec_StartConfiguredWaveform(&tMircoelec, eWaveType,
            wAHz, wBHz, (uint8_t)wBAmplitudePercent);
        if (eResult == DAC_DRIVER_RESULT_OK) {
            MLOGF(I, "DAC started: mode=%s A=%lu Hz B=%lu Hz B_amp=%lu%%\r\n",
                  (eWaveType == DAC_DRIVER_WAVE_DIGITAL) ? "dig" : "sin",
                  (unsigned long)wAHz, (unsigned long)wBHz,
                  (unsigned long)wBAmplitudePercent);
        } else {
            MLOGF(W, "DAC start failed: %d\r\n", (int)eResult);
        }
        return;
    }

    if (mshell_ConsumeWord(&pchText, "stop")
        && mshell_AtEnd(pchText)) {
        eResult = mircoelec_StopWaveform(&tMircoelec);
        if (eResult == DAC_DRIVER_RESULT_OK) {
            MLOGF(I, "DAC output stopped\r\n");
        } else {
            MLOGF(W, "DAC stop failed: %d\r\n", (int)eResult);
        }
        return;
    }
    MLOGF(W, "Usage: dac start [dig|sin] <A_Hz> <B_Hz> | dac stop\r\n");
}

MODUS_SHELL_CMD(dac, _debug_DacCommand,
                "DAC waveform: start [dig|sin] <A_Hz> <B_Hz> [B_percent] "
                "| stop");
#endif

/***********************************************************
 *  skeleton.c
 *  Example for ping-pong processing
 *  Caution: It is intended, that this file ist not runnable.
 *  The file contains mistakes and omissions, which shall be
 *  corrected and completed by the students.
 *
 *   F. Quint, HsKA
 *
 *
 ************************************************************/

#include <my_dsp_projectcfg.h>
#include <std.h>
#include <csl.h>
#include <csl_mcbsp.h>
#include <csl_irq.h>
#include <csl_edma.h>
#include <swi.h>
#include <sem.h>
#include <dsk6713_led.h>
#include <dsk6713.h>
#include "config_AIC23.h"
#include "config_BSPLink.h"
#include "Sounds.h"
#include "skeleton.h"

Uint32 ringbuff_audio_in_read_i = RINGBUFFER_LEN / 2;
Uint32 ringbuff_audio_in_write_i = 0;

Uint32 ringbuff_audio_out_read_i = RINGBUFFER_LEN / 2;
Uint32 ringbuff_audio_out_write_i = 0;

Uint32 Decoding_Buffer_i = 0;
Uint8 decoding_buff_valid = 0;

Uint32 Encoding_Buffer_i = 0;
Uint8 encoding_buff_valid = 0;

Uint8 dataDetected = 0;

Uint32 soundBuffer_i = 0;

Uint16 time_cnt = 0;

int configComplete = 0;
Uint8 t_reg = 0;

main() {
	Uint16 i = 0;

	DSK6713_init();
	CSL_init();

	for (i = 0; i < AIC_BUFFER_LEN; i++) {
//		Debug_Buff_ping[i] = LINK_PREAM_START;
//		Debug_Buff_pong[AIC_BUFFER_LEN-i-1] = LINK_PREAM_START;
		Debug_Buff_ping[i] = (short) i;
		Debug_Buff_pong[AIC_BUFFER_LEN - i - 1] = (short) i + 1;
	}

	/* Configure McBSP0 and AIC23 */
	Config_DSK6713_AIC23();

	/* Configure McBSP1*/
	hMcbsp_AIC23 = MCBSP_open(MCBSP_DEV1, MCBSP_OPEN_RESET);
	MCBSP_config(hMcbsp_AIC23, &datainterface_config);

	/* configure EDMA */
	config_AIC23_EDMA();

	//DSK6713_LED_on(0);
	//DSK6713_LED_on(1);
	//DSK6713_LED_on(2);
	//DSK6713_LED_on(3);

	/* finally the interrupts */
	config_interrupts();

	MCBSP_start(hMcbsp_AIC23, MCBSP_XMIT_START | MCBSP_RCV_START, 0xffffffff); // Start Audio IN & OUT transmision
	MCBSP_write(hMcbsp_AIC23, 0x0); /* one shot */

	configComplete = 1;

} /* finished*/

void config_AIC23_EDMA(void) {
	/* RECEIVE */
	/* Konfiguration der EDMA zum Lesen*/
	hEdmaRcv = EDMA_open(EDMA_CHA_REVT1, EDMA_OPEN_RESET); // EDMA Kanal f�r das Event REVT1
	hEdmaRcvRelPing = EDMA_allocTable(-1); // einen Reload-Parametersatz f�r Ping
	hEdmaRcvRelPong = EDMA_allocTable(-1); // einen Reload-Parametersatz f�r Pong

	configEDMARcv.src = MCBSP_getRcvAddr(hMcbsp_AIC23); //  Quell-Adresse zum Lesen

	tccRcvPing = EDMA_intAlloc(-1); // n�chsten freien Transfer-Complete-Code Ping
	tccRcvPong = EDMA_intAlloc(-1); // n�chsten freien Transfer-Complete-Code Pong
	configEDMARcv.opt |= EDMA_FMK(OPT, TCC, tccRcvPing); // dann der Grundkonfiguration des EDMA Empfangskanals zuweisen

	/* ersten Transfer und Reload-Ping mit ConfigPing konfigurieren */
	EDMA_config(hEdmaRcv, &configEDMARcv);
	EDMA_config(hEdmaRcvRelPing, &configEDMARcv);

	/* braucht man auch noch andere EDMA-Konfigurationen fuer das Lesen? ja -> pong */
	configEDMARcv.opt &= 0xFFF0FFFF;
	configEDMARcv.opt |= EDMA_FMK(OPT, TCC, tccRcvPong);
	configEDMARcv.dst = (Uint32) AIC_Buffer_in_pong;
	EDMA_config(hEdmaRcvRelPong, &configEDMARcv);

	/* Transfers verlinken ping -> pong -> ping */
	EDMA_link(hEdmaRcv, hEdmaRcvRelPong); /* noch mehr verlinken? */
	EDMA_link(hEdmaRcvRelPong, hEdmaRcvRelPing);
	EDMA_link(hEdmaRcvRelPing, hEdmaRcvRelPong);

	/* TRANSMIT */
	/* Konfiguration der EDMA zum Schreiben */
	hEdmaXmt = EDMA_open(EDMA_CHA_XEVT1, EDMA_OPEN_RESET); // EDMA Kanal f�r das Event REVT1
	hEdmaXmtRelPing = EDMA_allocTable(-1); // einen Reload-Parametersatz f�r Ping
	hEdmaXmtRelPong = EDMA_allocTable(-1); // einen Reload-Parametersatz f�r Pong

	configEDMAXmt.dst = MCBSP_getXmtAddr(hMcbsp_AIC23);	//  Ziel-Adresse zum Schreiben

	tccXmtPing = EDMA_intAlloc(-1); // n�chsten freien Transfer-Complete-Code Ping
	tccXmtPong = EDMA_intAlloc(-1); // n�chsten freien Transfer-Complete-Code Pong
	configEDMAXmt.opt |= EDMA_FMK(OPT, TCC, tccXmtPing); // dann der Grundkonfiguration des EDMA Sendekanal zuweisen

	/* ersten Transfer und Reload-Ping mit ConfigPing konfigurieren */
	EDMA_config(hEdmaXmt, &configEDMAXmt);
	EDMA_config(hEdmaXmtRelPing, &configEDMAXmt);

	/* braucht man auch noch andere EDMA-Konfigurationen fuer das Schreiben? ja -> pong */
	configEDMAXmt.opt &= 0xFFF0FFFF;
	configEDMAXmt.opt |= EDMA_FMK(OPT, TCC, tccXmtPong);
	configEDMAXmt.src = (Uint32) AIC_Buffer_out_pong;
	EDMA_config(hEdmaXmtRelPong, &configEDMAXmt);

	/* Transfers verlinken ping -> pong -> ping */
	EDMA_link(hEdmaXmt, hEdmaXmtRelPong); /* noch mehr verlinken? */
	EDMA_link(hEdmaXmtRelPong, hEdmaXmtRelPing);
	EDMA_link(hEdmaXmtRelPing, hEdmaXmtRelPong);

	/* EDMA TCC-Interrupts freigeben */
	EDMA_intClear(tccRcvPing);
	EDMA_intEnable(tccRcvPing);
	EDMA_intClear(tccRcvPong);
	EDMA_intEnable(tccRcvPong);
	/* sind das alle? nein -> pong und alle f�r Sendeseite */
	EDMA_intClear(tccXmtPing);
	EDMA_intEnable(tccXmtPing);
	EDMA_intClear(tccXmtPong);
	EDMA_intEnable(tccXmtPong);

	/* EDMA starten, wen alles? */
	EDMA_enableChannel(hEdmaRcv);
	EDMA_enableChannel(hEdmaXmt);
}

void config_interrupts(void) {
	IRQ_map(IRQ_EVT_EDMAINT, 8);		// CHECK same settings in BIOS!!!
	IRQ_clear(IRQ_EVT_EDMAINT);
	IRQ_enable(IRQ_EVT_EDMAINT);

	SWI_enable();

	IRQ_globalEnable();
}

void EDMA_ISR(void) {

	DSK6713_LED_on(2);
	static volatile int rcvPingDone = 0;	//static
	static volatile int rcvPongDone = 0;
	static volatile int xmtPingDone = 0;
	static volatile int xmtPongDone = 0;

	// BSP Data Link Interface
	static volatile int xmtBSPLinkPingDone = 0;
	static volatile int xmtBSPLinkPongDone = 0;

	static volatile int rcvBSPLinkPingDone = 0;
	static volatile int rcvBSPLinkPongDone = 0;

	if (EDMA_intTest(tccRcvPing)) {
		EDMA_intClear(tccRcvPing); // clear is mandatory
		rcvPingDone = 1;
	} else if (EDMA_intTest(tccRcvPong)) {
		EDMA_intClear(tccRcvPong);
		rcvPongDone = 1;
	}

	if (EDMA_intTest(tccXmtPing)) {
		EDMA_intClear(tccXmtPing);
		xmtPingDone = 1;
	} else if (EDMA_intTest(tccXmtPong)) {
		EDMA_intClear(tccXmtPong);
		xmtPongDone = 1;
	}

	/*
	 * *********** BSP Data Link Interface **************
	 */
	// Transmit
	if (EDMA_intTest(tccBSPLinkXmtPing)) {
		EDMA_intClear(tccBSPLinkXmtPing);
		xmtBSPLinkPingDone = 1;
	} else if (EDMA_intTest(tccBSPLinkXmtPong)) {
		EDMA_intClear(tccBSPLinkXmtPong);
		xmtBSPLinkPongDone = 1;
	}

	// Receive
	if (EDMA_intTest(tccBSPLinkRcvPing)) {
		EDMA_intClear(tccBSPLinkRcvPing);
		rcvBSPLinkPingDone = 1;

	} else if (EDMA_intTest(tccBSPLinkRcvPong)) {
		EDMA_intClear(tccBSPLinkRcvPong);
		rcvBSPLinkPongDone = 1;
	}

	/*
	 * *********** DECODER **********************
	 *
	 *  BSPLink_in_ Ping/Pong -> Ringbuffer_in -> ADC Out ping/pong
	 */
	if (rcvBSPLinkPingDone) {
		rcvBSPLinkPingDone = 0;
		DSK6713_LED_on(0);
		SWI_post(&SWI_BSPLink_In_Ping);
	} else if (rcvBSPLinkPongDone) {
		rcvBSPLinkPongDone = 0;
		DSK6713_LED_on(0);
		SWI_post(&SWI_BSPLink_In_Pong);
	}

// Ringbuffer lesen -> ADC schreiben
	if (xmtPingDone) {

		xmtPingDone = 0;
		SWI_post(&SWI_ADC_Out_Ping);
	} else if (xmtPongDone) {

		xmtPongDone = 0;
		SWI_post(&SWI_ADC_Out_Pong);
	}
	/*
	 * *********** ENCODER **********************
	 *
	 *  ADC_in_ping/pong -> BSPLink_out ping/pong
	 */
	if (xmtBSPLinkPingDone) {
		DSK6713_LED_on(1);
		xmtBSPLinkPingDone = 0;
		SWI_post(&SWI_BSPLink_Out_Ping);
	} else if (xmtBSPLinkPongDone) {
		DSK6713_LED_on(1);
		xmtBSPLinkPongDone = 0;
		SWI_post(&SWI_BSPLink_Out_Pong);
	}

	// ADC lesen -> Ringbuffer schreiben
	if (rcvPingDone) {

		rcvPingDone = 0;
		SWI_post(&SWI_ADC_In_Ping);
	} else if (rcvPongDone) {

		rcvPongDone = 0;
		SWI_post(&SWI_ADC_In_Pong);
	}

	DSK6713_LED_off(2);
}

/************************ SWI Section ****************************************/

// BSP Input RingBuffer lesen
void ADC_Out_Ping(void) {
	read_buffer_audio_out(AIC_Buffer_out_ping);
}

void ADC_Out_Pong(void) {
	read_buffer_audio_out(AIC_Buffer_out_pong);

}

void ADC_In_Ping(void) {
#ifdef SEND_DEBUG_BUFFER
	write_buffer_audio_in(Debug_Buff_ping);
#else
	write_buffer_audio_in(AIC_Buffer_in_ping);
#endif

	SWI_post(&SWI_Encode_Buffer);

}

void ADC_In_Pong(void) {
#ifdef SEND_DEBUG_BUFFER
	write_buffer_audio_in(Debug_Buff_pong);
#else
	write_buffer_audio_in(AIC_Buffer_in_pong);
#endif

	SWI_post(&SWI_Encode_Buffer);
}

// BSP Input RingBuffer schreiben
void BSPLink_In_Ping(void) {

	write_decoding_buffer(BSPLinkBuffer_in_ping);
	DSK6713_LED_off(0);

}

void BSPLink_In_Pong(void) {

	write_decoding_buffer(BSPLinkBuffer_in_pong);
	DSK6713_LED_off(0);
}

// BSP Input RingBuffer schreiben
void BSPLink_Out_Ping(void) {

	Uint16 i_write;
	if (encoding_buff_valid) {

		framing_link_data(BSPLinkBuffer_out_ping);
		encoding_buff_valid = 0;
	} else {
		// Encoding Buffer hat neue Werte
		for (i_write = 0; i_write < LINK_BUFFER_LEN; i_write++) {
			// Wenn encoder nicht bereit, STOP senden
			BSPLinkBuffer_out_ping[i_write] = LINK_PREAM_STOP;
		}

	}
	DSK6713_LED_off(1);
}

void BSPLink_Out_Pong(void) {

	Uint16 i_write;
	if (encoding_buff_valid) {

		framing_link_data(BSPLinkBuffer_out_pong);
		encoding_buff_valid = 0;

	} else {
		// Encoding Buffer hat neue Werte
		for (i_write = 0; i_write < LINK_BUFFER_LEN; i_write++) {
			// Wenn encoder nicht bereit, STOP senden
			BSPLinkBuffer_out_pong[i_write] = LINK_PREAM_STOP;
		}

	}
	DSK6713_LED_off(1);

}

void decode_buffer(void) {

	// Hier die Decodierung machen

	for (Decoding_Buffer_i = 0; Decoding_Buffer_i < DECODING_BUFF_LEN;
			Decoding_Buffer_i++) {
		// Decodingbuffer einmal auslesen - direkt kopieren, da keine codierung
		Ringbuffer_Audio_out[ringbuff_audio_out_write_i] =
				Decoding_Buffer[Decoding_Buffer_i];
		ringbuff_audio_out_write_i++;

		if (ringbuff_audio_out_write_i >= RINGBUFFER_LEN)
			ringbuff_audio_out_write_i = 0;
	}
	DSK6713_LED_toggle(3);
}

void encode_buffer(void) {
	// Hier Encoding Machen

	for (Encoding_Buffer_i = 0; Encoding_Buffer_i < ENCODING_BUFF_LEN;
			Encoding_Buffer_i++) {
		// Encodingbuffer einmal f�llen - direkt kopieren, da keine codierung
		Encoding_Buffer[Encoding_Buffer_i] =
				Ringbuffer_Audio_in[ringbuff_audio_in_read_i];
		ringbuff_audio_in_read_i++;

		if (ringbuff_audio_in_read_i >= RINGBUFFER_LEN)
			ringbuff_audio_in_read_i = 0;
	}

	encoding_buff_valid = 1;
}

/****************************************************************************/

void framing_link_data(short * bufferdes) {
	// Kapselt Encoding_Buffer mit START und STOP und schreibt in ping od. pong
	// BSP Output schreiben
	Uint16 Link_Buff_i = 0;
	Uint16 Enc_Buff_i = 0;

	for (Link_Buff_i = 0; Link_Buff_i < 20; Link_Buff_i++) {
		bufferdes[Link_Buff_i] = LINK_PREAM_START;
	}

	for (Enc_Buff_i = 0; Enc_Buff_i < ENCODING_BUFF_LEN; Enc_Buff_i++) {
		bufferdes[Link_Buff_i] = Encoding_Buffer[Enc_Buff_i];
		Link_Buff_i++;
	}

	for (; Link_Buff_i < LINK_BUFFER_LEN; Link_Buff_i++) {
		BSPLinkBuffer_out_ping[Link_Buff_i] = LINK_PREAM_STOP;
	}
}

void write_decoding_buffer(short * buffersrc) {
// Daten extrahieren, Decoder f�llen

	Uint32 i_read;

	for (i_read = 0; i_read < LINK_BUFFER_LEN; i_read++) {
		// Ringbuffer einmal durchlaufen

		Debug_Buff[debug_buff_i] = buffersrc[i_read];
		debug_buff_i++;
		if (debug_buff_i >= 30000)
			debug_buff_i = 0;

		if (buffersrc[i_read] == LINK_PREAM_STOP) {
			if (dataDetected == 1)
				dataDetected = 0;
			else if (dataDetected == 2)
				dataDetected = 3;

		} else if (buffersrc[i_read] == LINK_PREAM_START) {
			dataDetected = 1;
			continue;

		} else if (dataDetected == 1) {
			// Erstes Datum nach Start-Pre�mble
			Decoding_Buffer_i = 0;	// Buffer Index an Anfang stellen
			dataDetected = 2;
		}

		if (dataDetected == 2) {
			Decoding_Buffer[Decoding_Buffer_i] = buffersrc[i_read];
			Decoding_Buffer_i++;
		} else if (dataDetected == 3) {
			/* Stop detected. Buffer Valid */
			// Nach STOP: Buffer mit Nullen f�llen
			DSK6713_LED_toggle(3);
			//SWI_post(&SWI_Decode_Buffer);
			dataDetected = 0;
			break;
//			Decoding_Buffer[Decoding_Buffer_i] = 0;
//			Decoding_Buffer_i++;
		}

		if (Decoding_Buffer_i >= DECODING_BUFF_LEN) {
			Decoding_Buffer_i = 0;
			break;
		}

	}

}

void read_buffer_audio_out(short * bufferdes) {
// Ringbuffer nach BSPLink schreiben

	Uint32 i_write;

	for (i_write = 0; i_write < AIC_BUFFER_LEN - 1; i_write = i_write + 2) {
		// Ringbuffer einmal durchlaufen
		bufferdes[i_write] = Ringbuffer_Audio_out[ringbuff_audio_out_read_i];
		bufferdes[i_write + 1] =
				Ringbuffer_Audio_out[ringbuff_audio_out_read_i];

		ringbuff_audio_out_read_i++;

		if (ringbuff_audio_out_read_i >= RINGBUFFER_LEN)
			ringbuff_audio_out_read_i = 0;
	}
}

void write_buffer_audio_in(short * bufferscr) {
// Audiodaten als MONO Signal in Buffer schreiben

	Uint32 i_read;

	for (i_read = 0; i_read < AIC_BUFFER_LEN - 1; i_read = i_read + 2) {
		// Ringbuffer einmal durchlaufen

		Ringbuffer_Audio_in[ringbuff_audio_in_write_i] =
				(short) (bufferscr[i_read] / 2 + bufferscr[i_read + 1] / 2);

		ringbuff_audio_in_write_i++;

		if (ringbuff_audio_in_write_i >= RINGBUFFER_LEN)
			ringbuff_audio_in_write_i = 0;
	}

}

/* Periodic Function */
void SWI_LEDToggle(void) {
	SEM_postBinary(&SEM_LEDToggle);
}

void tsk_led_toggle(void) {

	/* 1 sec Tick */
	while (1) {
		SEM_pendBinary(&SEM_LEDToggle, SYS_FOREVER);

		if (configComplete == 1) {

			MCBSP_close(hMcbsp_AIC23_Config);

			/* Set McBSP0 MUX to EXTERN */
			t_reg = DSK6713_rget(DSK6713_MISC);
			t_reg |= MCBSP1SEL;				// Set MCBSP0 to 1 (extern)
			DSK6713_rset(DSK6713_MISC, t_reg);

			/* configure BSPLink-Interface */
			config_BSPLink();

			configComplete = 2;

		} else if (configComplete == 2) {
		}

	}
}

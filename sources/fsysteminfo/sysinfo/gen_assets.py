#!/usr/bin/env python3
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

a = Assets("SYSINFO")
a.text("T_TITLE", "SYSTEM INFO")
a.table("T_SECTION", ["FIRMWARE", "SYSTEM", "BATTERY", "MEMORY", "IDENTITY"])
a.table("T_KEY", ["EDITION", "VERSION", "BUILD", "TIME", "BIN CRC32",
                       "FLASH", "RAM TOTAL", "FREQ", "OVERLAY", "LEVEL", "TYPE",
                       "CHANNELS", "LISTS", "HF", "VHF", "UHF", "AERO", "OTHER",
                       "UID1", "UID2", "UID3", "UPTIME", "AM/FM",
                       "WIDE/NARROW", "CTCSS/DCS", "CPUID", "MCU TEMP", "CPU",
                       "RESET", "SILICON REV", "CLOCK", "RDP", "VDD", "BOR",
                       "HW FLASH", "MEM MAP", "IWDG", "FLASH WS", "WWDG",
                       "NRST", "IWDG STOP", "SPI FLASH", "FREE NOW",
                       "MIN SAMPLED", "STACK SAMP", "BANDS"])
a.table("T_BATTERY", ["1600mAh UV-K5", "2200mAh UV-K5", "3500mAh UV-K5", "1400mAh UV-K1",
                             "2500mAh UV-K1", "1400mAh UV-K1", "UNKNOWN"])
a.text("T_CPU", "PY32F071")
a.table("T_RESET", ["POWER", "PIN", "SOFTWARE", "IWDG", "WWDG", "OPTION",
                    "UNKNOWN"])
a.table("T_CLOCK", ["HSI", "HSE", "PLL", "LSI", "LSE", "UNKNOWN"])
a.table("T_RDP", ["OPEN", "LOCKED", "UNKNOWN"])
a.table("T_BOR", ["OFF", "1.7-1.8V", "1.9-2.0V", "2.1-2.2V", "2.3-2.4V",
                  "2.5-2.6V", "2.7-2.8V", "2.9-3.0V", "3.1-3.2V"])
a.table("T_BOOT", ["FLASH", "SYSTEM", "SRAM", "UNKNOWN"])
a.table("T_WDG", ["HARDWARE", "SOFTWARE"])
a.table("T_NRST", ["RESET", "GPIO"])
a.table("T_IWDG_STOP", ["FREEZE", "ACTIVE"])
a.text("T_HW_FLASH", "128K")
a.text("T_SPI_FLASH", "PY25Q16 2M")
a.main()

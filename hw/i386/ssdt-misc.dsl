/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

ACPI_EXTRACT_ALL_CODE ssdp_misc_aml

DefinitionBlock ("ssdt-misc.aml", "SSDT", 0x01, "BXPC", "BXSSDTSUSP", 0x1)
{

/****************************************************************
 * PCI memory ranges
 ****************************************************************/

    Scope(\) {
       ACPI_EXTRACT_NAME_DWORD_CONST acpi_pci32_start
       Name(P0S, 0x12345678)
       ACPI_EXTRACT_NAME_DWORD_CONST acpi_pci32_end
       Name(P0E, 0x12345678)
       ACPI_EXTRACT_NAME_BYTE_CONST acpi_pci64_valid
       Name(P1V, 0x12)
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_start
       Name(P1S, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_end
       Name(P1E, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_length
       Name(P1L, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
    }


/****************************************************************
 * Suspend
 ****************************************************************/

    Scope(\) {
    /*
     * S3 (suspend-to-ram), S4 (suspend-to-disk) and S5 (power-off) type codes:
     * must match piix4 emulation.
     */

        ACPI_EXTRACT_NAME_STRING acpi_s3_name
        Name(_S3, Package(0x04) {
            One,  /* PM1a_CNT.SLP_TYP */
            One,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
        ACPI_EXTRACT_NAME_STRING acpi_s4_name
        ACPI_EXTRACT_PKG_START acpi_s4_pkg
        Name(_S4, Package(0x04) {
            0x2,  /* PM1a_CNT.SLP_TYP */
            0x2,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
        Name(_S5, Package(0x04) {
            Zero,  /* PM1a_CNT.SLP_TYP */
            Zero,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
    }

    External(\_SB.PCI0, DeviceObj)
    External(\_SB.PCI0.ISA, DeviceObj)

    Scope(\_SB.PCI0.ISA) {
        Device(PEVT) {
            Name(_HID, "QEMU0001")
            /* PEST will be patched to be Zero if no such device */
            ACPI_EXTRACT_NAME_WORD_CONST ssdt_isa_pest
            Name(PEST, 0xFFFF)
            OperationRegion(PEOR, SystemIO, PEST, 0x01)
            Field(PEOR, ByteAcc, NoLock, Preserve) {
                PEPT,   8,
            }

            Method(_STA, 0, NotSerialized) {
                Store(PEST, Local0)
                If (LEqual(Local0, Zero)) {
                    Return (0x00)
                } Else {
                    Return (0x0F)
                }
            }

            Method(RDPT, 0, NotSerialized) {
                Store(PEPT, Local0)
                Return (Local0)
            }

            Method(WRPT, 1, NotSerialized) {
                Store(Arg0, PEPT)
            }

            Name(_CRS, ResourceTemplate() {
                IO(Decode16, 0x00, 0x00, 0x01, 0x01, IO)
            })

            CreateWordField(_CRS, IO._MIN, IOMN)
            CreateWordField(_CRS, IO._MAX, IOMX)

            Method(_INI, 0, NotSerialized) {
                Store(PEST, IOMN)
                Store(PEST, IOMX)
            }
        }
    }
    Scope(\_SB) {
        External(NTFY, MethodObj)
        External(CPON, PkgObj)

        Device(CPHD) {
            Name(_HID, EISAID("PNP0C08"))
            Name(CPPL, 32) // cpu-gpe length
            ACPI_EXTRACT_NAME_WORD_CONST ssdt_cpugpe_port
            Name(CPHP, 0xaf00)

            OperationRegion(PRST, SystemIO, CPHP, CPPL)
            Field(PRST, ByteAcc, NoLock, Preserve) {
                PRS, 256
            }

            Method(PRSC, 0) {
                // Local5 = active cpu bitmap
                Store(PRS, Local5)
                // Local2 = last read byte from bitmap
                Store(Zero, Local2)
                // Local0 = Processor ID / APIC ID iterator
                Store(Zero, Local0)
                While (LLess(Local0, SizeOf(CPON))) {
                    // Local1 = CPON flag for this cpu
                    Store(DerefOf(Index(CPON, Local0)), Local1)
                    If (And(Local0, 0x07)) {
                        // Shift down previously read bitmap byte
                        ShiftRight(Local2, 1, Local2)
                    } Else {
                        // Read next byte from cpu bitmap
                        Store(DerefOf(Index(Local5, ShiftRight(Local0, 3))), Local2)
                    }
                    // Local3 = active state for this cpu
                    Store(And(Local2, 1), Local3)

                    If (LNotEqual(Local1, Local3)) {
                        // State change - update CPON with new state
                        Store(Local3, Index(CPON, Local0))
                        // Do CPU notify
                        If (LEqual(Local3, 1)) {
                            NTFY(Local0, 1)
                        } Else {
                            NTFY(Local0, 3)
                        }
                    }
                    Increment(Local0)
                }
            }

            /* Leave bit 0 cleared to avoid Windows BSOD */
            Name(_STA, 0xA)

            Method(_CRS, 0) {
                Store(ResourceTemplate() {
                    IO(Decode16, 0x00, 0x00, 0x01, 0x15, IO)
                }, Local0)

                CreateWordField(Local0, IO._MIN, IOMN)
                CreateWordField(Local0, IO._MAX, IOMX)

                Store(CPHP, IOMN)
                Subtract(Add(CPHP, CPPL), 1, IOMX)
                Return(Local0)
            }
        } // Device(CPHD)
    } // Scope(\_SB)
}

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

/****************************************************************
 * CPU hotplug
 ****************************************************************/

Scope(\_SB) {
    /* Objects filled in by run-time generated SSDT */
    External(NTFY, MethodObj)
    External(\_SB.CPHD.PRSC, MethodObj)
    External(CPON, PkgObj)

    /* Methods called by run-time generated SSDT Processor objects */
    Method(CPMA, 1, NotSerialized) {
        // _MAT method - create an madt apic buffer
        // Arg0 = Processor ID = Local APIC ID
        // Local0 = CPON flag for this cpu
        Store(DerefOf(Index(CPON, Arg0)), Local0)
        // Local1 = Buffer (in madt apic form) to return
        Store(Buffer(8) {0x00, 0x08, 0x00, 0x00, 0x00, 0, 0, 0}, Local1)
        // Update the processor id, lapic id, and enable/disable status
        Store(Arg0, Index(Local1, 2))
        Store(Arg0, Index(Local1, 3))
        Store(Local0, Index(Local1, 4))
        Return (Local1)
    }
    Method(CPST, 1, NotSerialized) {
        // _STA method - return ON status of cpu
        // Arg0 = Processor ID = Local APIC ID
        // Local0 = CPON flag for this cpu
        Store(DerefOf(Index(CPON, Arg0)), Local0)
        If (Local0) {
            Return (0xF)
        } Else {
            Return (0x0)
        }
    }
    Method(CPEJ, 2, NotSerialized) {
        // _EJ0 method - eject callback
        Sleep(200)
    }
}

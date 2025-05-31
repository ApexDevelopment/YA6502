#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <iomanip>
#include "types.hpp"
#include "helpers.hpp"
#include "bin.hpp"
#include "rampage.cpp"

struct MMU {
	std::unique_ptr<MemoryPage> pages[256];

	void initialize() {
		for (int i = 0; i < 256; i++) {
			pages[i] = std::make_unique<RAMPage>();
		}
	}

	Byte read_byte(Word address) {
		Byte page_num = hi(address);
		Byte page_addr = lo(address);
		return pages[page_num]->read_byte(page_addr);
	}

	void write_byte(Word address, Byte value) {
		Byte page_num = hi(address);
		Byte page_addr = lo(address);
		pages[page_num]->write_byte(page_addr, value);
	}

	Word read_word(Word address) {
		// Little-endian
		Word byte_lo = widen(read_byte(address));
		Word byte_hi = widen(read_byte(address + 1)) << 8;
		return byte_lo | byte_hi;
	}
};

static Byte addr_mode_table[8][8] = {
	{ CPU_ADDR_MODE_IMM,     CPU_ADDR_MODE_ZPG, CPU_ADDR_MODE_INVALID, CPU_ADDR_MODE_ABS, CPU_ADDR_MODE_INVALID, CPU_ADDR_MODE_ZPX, CPU_ADDR_MODE_INVALID, CPU_ADDR_MODE_ABX },
	{ CPU_ADDR_MODE_ZPX_IND, CPU_ADDR_MODE_ZPG, CPU_ADDR_MODE_IMM,     CPU_ADDR_MODE_ABS, CPU_ADDR_MODE_ZPY_IND, CPU_ADDR_MODE_ZPX, CPU_ADDR_MODE_ABY,     CPU_ADDR_MODE_ABX },
	{ CPU_ADDR_MODE_IMM,     CPU_ADDR_MODE_ZPG, CPU_ADDR_MODE_ACC,     CPU_ADDR_MODE_ABS, CPU_ADDR_MODE_INVALID, CPU_ADDR_MODE_ZPX, CPU_ADDR_MODE_INVALID, CPU_ADDR_MODE_ABX },

	{ CPU_ADDR_MODE_INVALID },
	{ CPU_ADDR_MODE_INVALID },
	{ CPU_ADDR_MODE_INVALID },
	{ CPU_ADDR_MODE_INVALID },
	{ CPU_ADDR_MODE_INVALID }
};

struct CPU {
	unsigned long cycle_count = 0;
	
	CPUType type = MOS;
	Byte A, X, Y; // Registers
	Byte SP;      // Stack Pointer
	Word PC;      // Program Counter
	Byte SF;      // Status Flags
	Word addr_bus_value = 0;
	Byte data_bus_value = 0;

	Word last_good_instruction = 0;
	Word last_jump_origin = 0;
	Word last_jump_target = 0;

	std::vector<Word> breakpoints;

	void reset(MMU& mmu) {
		A = 0;
		X = 0;
		Y = 0;
		SP = 0xFD; // Stack pointer starts at 0x01FF, but is decremented first
		PC = mmu.read_word(0xFFFC); // Read reset vector
		SF = 0b00100100; // Processor status. No interrupts, no BCD mode, set break flag
		cycle_count = 7; // Takes 7 cycles to reset
	}

	void set_flag(Byte flag, Byte value) {
		if (value == 0) {
			SF &= ~flag;
		} else {
			SF |= flag;
		}
	}

	bool check_flag(Byte flag) {
		if (SF & flag) {
			return true;
		}
		return false;
	}

	void dump_state(MMU& mmu) {
		std::cout << "CPU State:" << std::endl;
		Byte instruction = mmu.read_byte(PC);
		Word next_word = mmu.read_word(PC + 1);
		std::cout << "Instruction: 0x" << std::hex << (int)instruction << std::endl;
		std::cout << "Next Word: 0x" << std::hex << (int)next_word << std::endl;
		std::cout << "A: 0x"  << std::hex << (int)A  << std::endl;
		std::cout << "X: 0x"  << std::hex << (int)X  << std::endl;
		std::cout << "Y: 0x"  << std::hex << (int)Y  << std::endl;
		std::cout << "SP: 0x" << std::hex << (int)SP << std::endl;
		std::cout << "PC: 0x" << std::hex <<      PC << std::endl;
		std::cout << "SF: 0b" << bin << (int)SF << std::endl << std::endl;
		std::cout << "Last known good instruction was at 0x" << std::hex << (int)last_good_instruction << std::endl;
		std::cout << "How did we get here? 0x" << std::hex << (int)last_jump_origin
			<< " jumped to 0x" << std::hex << (int)last_jump_target << std::endl;
	}

	std::string log_state(MMU& mmu) {
		Byte instruction = mmu.read_byte(PC);
		std::ostringstream oss;
		oss << std::hex << std::setw(4) << std::setfill('0') << (int)PC;
		oss << " " << std::hex << std::setw(2) << std::setfill('0') << (int)instruction;
		oss << std::setw(32) << std::setfill(' ') << "";
		oss << "A:" << std::hex << std::setw(2) << std::setfill('0') << (int)A;
		oss << " X:" << std::hex << std::setw(2) << std::setfill('0') << (int)X;
		oss << " Y:" << std::hex << std::setw(2) << std::setfill('0') << (int)Y;
		oss << " P:" << std::hex << std::setw(2) << std::setfill('0') << (int)SF;
		std::string result = oss.str();
		return result;
	}

	void exec_cycle(MMU& mmu, Byte micro_op) {
		switch (micro_op) {
			case CPU_UOP_FETCH:
			data_bus_value = mmu.read_byte(addr_bus_value);
			break;
			case CPU_UOP_WRITE:
			mmu.write_byte(addr_bus_value, data_bus_value);
			break;
			case CPU_UOP_NONE:
			default: break;
		}

		cycle_count++;
	}

	void stall_n_cycles(MMU& mmu, int n_cycles) {
		// Could probably just cycle_count+=n_cycles but whatever
		for (; n_cycles >= 0; n_cycles--) {
			exec_cycle(mmu, CPU_UOP_NONE);
		}
	}

	Byte fetch_one_byte(MMU& mmu, Word address) {
		addr_bus_value = address;
		exec_cycle(mmu, CPU_UOP_FETCH);
		return data_bus_value;
	}

	void write_one_byte(MMU& mmu, Word address, Byte value) {
		addr_bus_value = address;
		data_bus_value = value;
		exec_cycle(mmu, CPU_UOP_WRITE);
	}

	void stack_push(MMU& mmu, Byte value) {
		Word address = (Word)SP | 0x0100;
		write_one_byte(mmu, address, value);
		SP--;
	}

	Byte stack_pull(MMU& mmu) {
		SP++;
		Word address = (Word)SP | 0x0100;
		return fetch_one_byte(mmu, address);
	}

	void stack_push_status_flags(MMU& mmu) {
		// " The status register will be pushed with the break
		//   flag and bit 5 set to 1. "
		// https://www.masswerk.at/6502/6502_instruction_set.html
		// So if this is wrong blame those guys.
		stack_push(mmu, SF | CPU_FLAG_B | CPU_FLAG_UNUSED);
	}

	void stack_pull_status_flags(MMU& mmu) {
		// " The status register will be pulled with the break
		//   flag and bit 5 ignored. "
		Byte old_flags = SF;
		Byte new_flags = stack_pull(mmu);
		Byte retain = CPU_FLAG_B | CPU_FLAG_UNUSED;
		SF = (old_flags & retain) | (new_flags & ~retain);
	}

	Byte decode_addr_mode(Byte group, Byte opcode_encoded_mode) {
		return addr_mode_table[group][opcode_encoded_mode];
	}

	void auto_increment_pc(Byte addressing_mode) {
		switch (addressing_mode) {
			case CPU_ADDR_MODE_ACC:
			PC++;
			break;
			case CPU_ADDR_MODE_IMM:
			case CPU_ADDR_MODE_ZPG:
			case CPU_ADDR_MODE_ZPX:
			case CPU_ADDR_MODE_ZPY:
			case CPU_ADDR_MODE_ZPX_IND:
			case CPU_ADDR_MODE_ZPY_IND:
			PC += 2;
			break;
			case CPU_ADDR_MODE_ABS:
			case CPU_ADDR_MODE_ABX:
			case CPU_ADDR_MODE_ABY:
			PC += 3;
			break;
		}
	}

	// https://www.nesdev.org/obelisk-6502-guide/addressing.html
	// TODO: Handle the 6502's page boundary bugs
	Byte auto_fetch_value(MMU& mmu, Byte next_byte, Byte addressing_mode) {
		switch (addressing_mode) {
			case CPU_ADDR_MODE_IMM:
			return next_byte;
			case CPU_ADDR_MODE_ACC:
			return A;
			case CPU_ADDR_MODE_ZPG:
			return fetch_one_byte(mmu, widen(next_byte));
			case CPU_ADDR_MODE_ZPX: {
				// The 6502 wastes a cycle reading the unindexed ZP address
				(void)fetch_one_byte(mmu, widen(next_byte));
				return fetch_one_byte(mmu, lo(widen(next_byte) + X));
			}
			case CPU_ADDR_MODE_ZPY: {
				// The 6502 wastes a cycle reading the unindexed ZP address
				(void)fetch_one_byte(mmu, widen(next_byte));
				return fetch_one_byte(mmu, lo(widen(next_byte) + Y));
			}
			case CPU_ADDR_MODE_ABS: {
				Word high_addr_byte = widen(fetch_one_byte(mmu, PC + 2)) << 8;
				Word address = widen(next_byte) | high_addr_byte;
				return fetch_one_byte(mmu, address);
			}
			case CPU_ADDR_MODE_ABX: {
				Word high_addr_byte = widen(fetch_one_byte(mmu, PC + 2)) << 8;
				Word address = widen(next_byte) | high_addr_byte;
				return fetch_one_byte(mmu, address + X); // TODO: Page boundary
			}
			case CPU_ADDR_MODE_ABY: {
				Word high_addr_byte = widen(fetch_one_byte(mmu, PC + 2)) << 8;
				Word address = widen(next_byte) | high_addr_byte;
				return fetch_one_byte(mmu, address + Y); // TODO: Page boundary
			}
			case CPU_ADDR_MODE_ZPX_IND: {
				// ZPX Indexed Indirect addressing typically fetches from an address stored in a table residing in ZP
				Byte zp_indexed = lo(widen(next_byte) + X);
				// TODO: Does zp wrapping also occur here?
				Byte zp_indexed_next = lo(static_cast<Word>(widen(next_byte) + X + 1));
				Word addr_lo = widen(fetch_one_byte(mmu, zp_indexed));
				Word addr_hi = widen(fetch_one_byte(mmu, zp_indexed_next)) << 8;
				Word address = addr_lo | addr_hi; // Read address from table
				return fetch_one_byte(mmu, address); // Read from that address
			}
			// The NESDev Obelisk guide documents Indirect,Y incorrectly
			case CPU_ADDR_MODE_ZPY_IND: {
				// ZP contains pointer (base addr)
				Word addr_lo = widen(fetch_one_byte(mmu, widen(next_byte)));
				// TODO: Does zp wrapping also occur here?
				Word addr_hi = widen(fetch_one_byte(mmu, widen(static_cast<Byte>(next_byte + 1)))) << 8;
				Word address = (addr_lo | addr_hi) + Y;
				return fetch_one_byte(mmu, address); // Get it baby!
			}
			default: break;
		}
		return 0;
	}

	void auto_write_value(MMU& mmu, Byte next_byte, Byte addressing_mode, Byte value) {
		// NOTE: Perhaps percolate some kind of error on invalid memory ops? (e.g. writing in immediate mode)
		switch (addressing_mode) {
			case CPU_ADDR_MODE_ACC:
			A = value;
			break;
			case CPU_ADDR_MODE_ZPG:
			write_one_byte(mmu, widen(next_byte), value);
			break;
			case CPU_ADDR_MODE_ZPX: {
				// The 6502 wastes a cycle reading the unindexed ZP address
				(void)fetch_one_byte(mmu, widen(next_byte));
				write_one_byte(mmu, lo(widen(next_byte) + X), value);
				break;
			}
			case CPU_ADDR_MODE_ZPY: {
				// The 6502 wastes a cycle reading the unindexed ZP address
				(void)fetch_one_byte(mmu, widen(next_byte));
				write_one_byte(mmu, lo(widen(next_byte) + Y), value);
				break;
			}
			case CPU_ADDR_MODE_ABS: {
				Byte high_addr_byte = fetch_one_byte(mmu, PC + 2);
				Word address = make_address(next_byte, high_addr_byte);
				write_one_byte(mmu, address, value);
				break;
			}
			case CPU_ADDR_MODE_ABX: {
				Byte high_addr_byte = fetch_one_byte(mmu, PC + 2);
				Word address = make_address(next_byte, high_addr_byte);
				// TODO: Does the extra cycle from reading the unindexed address apply here?
				write_one_byte(mmu, address + X, value); // TODO: Page boundary
				break;
			}
			case CPU_ADDR_MODE_ABY: {
				Byte high_addr_byte = fetch_one_byte(mmu, PC + 2);
				Word address = make_address(next_byte, high_addr_byte);
				// TODO: Does the extra cycle from reading the unindexed address apply here?
				write_one_byte(mmu, address + Y, value); // TODO: Page boundary
				break;
			}
			case CPU_ADDR_MODE_ZPX_IND: {
				Byte zp_indexed = lo(widen(next_byte) + X);
				// TODO: Does zp wrapping also occur here?
				Byte zp_indexed_next = lo(static_cast<Word>(widen(next_byte) + X + 1));
				Word addr_lo = widen(fetch_one_byte(mmu, zp_indexed));
				Word addr_hi = widen(fetch_one_byte(mmu, zp_indexed_next)) << 8;
				Word address = addr_lo | addr_hi; // Read address from table
				write_one_byte(mmu, address, value);
				break;
			}
			case CPU_ADDR_MODE_ZPY_IND: {
				// ZP contains pointer (base addr)
				Word addr_lo = widen(fetch_one_byte(mmu, widen(next_byte)));
				Word addr_hi = widen(fetch_one_byte(mmu, widen(static_cast<Byte>(next_byte + 1)))) << 8;
				Word address = (addr_lo | addr_hi) + Y;
				write_one_byte(mmu, address, value);
				break;
			}
			default: break;
		}
	}

	bool nibble_add(Byte a, Byte b, Byte c, Byte& d) {
		Byte result = static_cast<Byte>(a + b + c);
		d = result & 0xF_b;
		return (check_flag(CPU_FLAG_D) && type != NES) ? result > 0x9 : result > 0xF;
	}

	Byte adjust_decimal(Byte b) {
		if (!check_flag(CPU_FLAG_D) || type == NES) {
			return b;
		}

		Byte lo_nib = b & 0xF_b;
		Byte hi_nib = (b & 0xF0_b) >> 4;
		lo_nib %= 0xA;
		hi_nib %= 0xA;
		return lo_nib | (hi_nib << 4);
	}

	// http://www.6502.org/tutorials/decimal_mode.html#A
	// https://forums.atariage.com/topic/163876-flags-on-decimal-mode-on-the-nmos-6502
	// https://c74project.com/card-b-alu-cu/
	void full_add(Byte operand) {
		Byte o_lo_nib = operand & 0xF_b;
		Byte o_hi_nib = (operand & 0xF0_b) >> 4;
		Byte A_lo_nib = A & 0xF_b;
		Byte A_hi_nib = (A & 0xF0_b) >> 4;

		Byte result_lo_nib = 0;
		Byte result_hi_nib = 0;
		bool half_carry = nibble_add(A_lo_nib, o_lo_nib, check_flag(CPU_FLAG_C), result_lo_nib);
		bool carry_out = nibble_add(A_hi_nib, o_hi_nib, half_carry, result_hi_nib);
		Byte result = result_lo_nib | (result_hi_nib << 4);
		
		set_flag(CPU_FLAG_C, carry_out);
		set_flag(CPU_FLAG_Z, result == 0);
		set_flag(CPU_FLAG_V, (~(A ^ operand) & (A ^ result)) & 0b10000000);
		set_flag(CPU_FLAG_N, result & 0b10000000);

		A = adjust_decimal(result);
	}

	// https://www.nesdev.org/obelisk-6502-guide/reference.html
	// https://llx.com/Neil/a2/opcodes.html
	CPUStatus exec_instruction(MMU& mmu, bool bypass_breakpoints) {
		if (!bypass_breakpoints && std::count(breakpoints.begin(), breakpoints.end(), PC) > 0) {
			return BREAKPOINT;
		}

		addr_bus_value = PC;
		exec_cycle(mmu, CPU_UOP_FETCH);
		Byte instruction = data_bus_value;
		
		// " All single-byte instructions waste a cycle reading and ignoring
		//   the byte that comes immediately after the instruction. "
		// - Sun Tzu, The Art of 6502
		addr_bus_value = PC + 1;
		exec_cycle(mmu, CPU_UOP_FETCH);
		Byte next_byte = data_bus_value;

		Byte aaa = (instruction & 0b11100000) >> 5; // Opcode
		Byte bbb = (instruction & 0b00011100) >> 2; // Addressing Mode
		Byte cc  = (instruction & 0b00000011);      // Opcode group
		Byte final_addr_mode = decode_addr_mode(cc, bbb);

		bool complex_instruction = false;
		Word old_pc = PC;

		// First we'll handle the stray one-byte instructions
		switch (instruction) {
			case 0xEA: break; // NOP
			case 0x00: {
				// BRK
				Word to_push = PC + 2;
				stack_push(mmu, hi(to_push));
				stack_push(mmu, lo(to_push));
				stack_push_status_flags(mmu);
				// https://www.masswerk.at/6502/6502_instruction_set.html#BRK
				// These guys say BRK does not disable interrupts, but everywhere
				// else I look says it does.
				SF |= CPU_FLAG_B | CPU_FLAG_I;
				Word interrupt_vector = make_address(fetch_one_byte(mmu, 0xFFFE), fetch_one_byte(mmu, 0xFFFF));
				last_jump_origin = PC;
				last_jump_target = interrupt_vector;
				PC = interrupt_vector - 1; // -1 to compensate for later PC++
				break;
			}
			case 0x40:{
				// RTI
				// TODO: Does flag B come from the stack or not???
				stack_pull_status_flags(mmu);
				Byte b_lo = stack_pull(mmu);
				Byte b_hi = stack_pull(mmu);
				Word target = make_address(b_lo, b_hi);
				last_jump_origin = PC;
				last_jump_target = target;
				PC = target - 1; // Compensate
				break;
			}
			case 0x60: {
				// RTS
				Byte b_lo = stack_pull(mmu);
				Byte b_hi = stack_pull(mmu);
				Word target = make_address(b_lo, b_hi);
				last_jump_origin = PC;
				last_jump_target = target + 1;
				// Normally we would compensate for PC++ by subtracting 1.
				// However, JSR pushes the return address minus 1.
				// So, in this case, we want PC++ to happen.
				PC = target;
				break;
			}

			// Flag manipulation instructions
			case 0x18:
			// CLC
			set_flag(CPU_FLAG_C, 0);
			break;
			case 0x38:
			// SEC
			set_flag(CPU_FLAG_C, 1);
			break;
			case 0x58:
			// CLI
			set_flag(CPU_FLAG_I, 0);
			break;
			case 0x78:
			// SEI
			set_flag(CPU_FLAG_I, 1);
			break;
			case 0xB8:
			// CLV
			set_flag(CPU_FLAG_V, 0);
			break;
			case 0xD8:
			// CLD
			set_flag(CPU_FLAG_D, 0);
			break;
			case 0xF8:
			// SED
			set_flag(CPU_FLAG_D, 1);
			break;

			// Register transfer instructions
			case 0xA8:
			// TAY
			Y = A;
			set_flag(CPU_FLAG_Z, Y == 0);
			set_flag(CPU_FLAG_N, Y & 0b10000000);
			break;
			case 0x98:
			// TYA
			A = Y;
			set_flag(CPU_FLAG_Z, A == 0);
			set_flag(CPU_FLAG_N, A & 0b10000000);
			break;
			case 0xAA:
			// TAX
			X = A;
			set_flag(CPU_FLAG_Z, X == 0);
			set_flag(CPU_FLAG_N, X & 0b10000000);
			break;
			case 0x8A:
			// TXA
			A = X;
			set_flag(CPU_FLAG_Z, A == 0);
			set_flag(CPU_FLAG_N, A & 0b10000000);
			break;
			case 0x9A:
			// TXS
			SP = X;
			break;
			case 0xBA:
			// TSX
			X = SP;
			set_flag(CPU_FLAG_Z, X == 0);
			set_flag(CPU_FLAG_N, X & 0b10000000);
			break;

			// Stack instructions
			case 0x08:
			// PHP
			stack_push_status_flags(mmu);
			break;
			case 0x28:
			// PLP
			stack_pull_status_flags(mmu);
			break;
			case 0x48:
			// PHA
			stack_push(mmu, A);
			break;
			case 0x68:
			// PLA
			A = stack_pull(mmu);
			set_flag(CPU_FLAG_Z, A == 0);
			set_flag(CPU_FLAG_N, A & 0b10000000);
			break;

			// Increment and decrement instructions
			case 0xC8:
			// INY
			Y++;
			set_flag(CPU_FLAG_Z, Y == 0);
			set_flag(CPU_FLAG_N, Y & 0b10000000);
			break;
			case 0x88:
			// DEY
			Y--;
			set_flag(CPU_FLAG_Z, Y == 0);
			set_flag(CPU_FLAG_N, Y & 0b10000000);
			break;
			case 0xE8:
			// INX
			X++;
			set_flag(CPU_FLAG_Z, X == 0);
			set_flag(CPU_FLAG_N, X & 0b10000000);
			break;
			case 0xCA:
			// DEX
			X--;
			set_flag(CPU_FLAG_Z, X == 0);
			set_flag(CPU_FLAG_N, X & 0b10000000);
			break;

			// Odd one out:
			case 0x20: {
				// JSR
				// PC + 3 is the address of the next instruction.
				// JSR pushes next_instruction_addr - 1, in essence PC + 2.
				Word return_addr = PC + 2;
				stack_push(mmu, hi(return_addr));
				stack_push(mmu, lo(return_addr));
				Word target = make_address(next_byte, fetch_one_byte(mmu, PC + 2));
				last_jump_origin = PC;
				last_jump_target = target;
				PC = target - 1; // Compensate
				break;
			}

			default: complex_instruction = true; break;
		}

		if (!complex_instruction) {
			PC++;
			return CONTINUE;
		}

		switch (cc) {
			case 0b01: // Group 1
			switch (aaa) {
				case 0b000: {
					// ORA - Logical OR
					A |= auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_Z, A == 0);
					set_flag(CPU_FLAG_N, A & 0b10000000);
					break;
				}
				case 0b001: {
					// AND - Logical AND
					A &= auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_Z, A == 0);
					set_flag(CPU_FLAG_N, A & 0b10000000);
					break;
				}
				case 0b010: {
					// EOR - Logical Exclusive OR
					A ^= auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_Z, A == 0);
					set_flag(CPU_FLAG_N, A & 0b10000000);
					break;
				}
				case 0b011: {
					// ADC - Add with Carry
					Byte operand = auto_fetch_value(mmu, next_byte, final_addr_mode);
					full_add(operand);
					
					// Word result = static_cast<Word>(
					// 	widen(A)
					// 	+ widen(operand)
					// 	+ widen(check_flag(CPU_FLAG_C)));
					// A = lo(result);
					// set_flag(CPU_FLAG_C, result > 0xFF); // In BCD mode this is 0x99 instead of 0xFF
					// set_flag(CPU_FLAG_Z, A == 0); // Works the same in BCD mode
					// set_flag(CPU_FLAG_V, (~(A ^ operand) & (A ^ result)) & 0b10000000);
					// set_flag(CPU_FLAG_N, A & 0b10000000); // Works the same in BCD mode
					break;
				}
				case 0b100: {
					// STA - Store Accumulator
					auto_write_value(mmu, next_byte, final_addr_mode, A);
					break;
				}
				case 0b101: {
					// LDA - Load Accumulator
					A = auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_Z, A == 0);
					set_flag(CPU_FLAG_N, A & 0b10000000);
					break;
				}
				case 0b110: {
					// CMP - Compare Accumulator
					Byte compare_mem = auto_fetch_value(mmu, next_byte, final_addr_mode);
					Word result = static_cast<Word>(A - compare_mem);
					
					// Gross...
					set_flag(CPU_FLAG_C, A >= result);
					set_flag(CPU_FLAG_Z, result == 0);
					set_flag(CPU_FLAG_N, static_cast<Byte>(result & 0b10000000));
					break;
				}
				case 0b111: {
					// SBC - Subtract with Carry
					Byte operand = ~auto_fetch_value(mmu, next_byte, final_addr_mode);
					full_add(operand);
					break;
				}
				default:
				PC++;
				return INVALID;
			}
			auto_increment_pc(final_addr_mode);
			break;
			case 0b10: // Group 2
			switch (aaa) {
				case 0b000: {
					// ASL - Arithmetic Shift Left
					Byte to_shift = auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_C, to_shift & 0b10000000);
					to_shift <<= 1;
					set_flag(CPU_FLAG_Z, to_shift == 0); // Documented incorrectly on NESdev?
					set_flag(CPU_FLAG_N, to_shift & 0b10000000);
					// TODO: Figure out how this works if we are in accumulator addressing mode
					auto_write_value(mmu, next_byte, final_addr_mode, to_shift);
					break;
				}
				case 0b001: {
					// ROL - Rotate Left
					Byte to_rotate = auto_fetch_value(mmu, next_byte, final_addr_mode);
					Byte old_carry = check_flag(CPU_FLAG_C);
					set_flag(CPU_FLAG_C, to_rotate & 0b10000000);
					to_rotate <<= 1;
					to_rotate |= old_carry;
					set_flag(CPU_FLAG_Z, to_rotate == 0); // Documented incorrectly on NESdev?
					set_flag(CPU_FLAG_N, to_rotate & 0b10000000);
					// TODO: Figure out how this works if we are in accumulator addressing mode
					auto_write_value(mmu, next_byte, final_addr_mode, to_rotate);
					break;
				}
				case 0b010: {
					// LSR - Logical Shift Right
					Byte to_shift = auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_C, to_shift & 1);
					to_shift >>= 1;
					set_flag(CPU_FLAG_Z, to_shift == 0); // Weirdly differs from the others on NESdev
					set_flag(CPU_FLAG_N, to_shift & 0b10000000);
					// TODO: Figure out how this works if we are in accumulator addressing mode
					auto_write_value(mmu, next_byte, final_addr_mode, to_shift);
					break;
				}
				case 0b011: {
					// ROR - Rotate Right
					Byte to_rotate = auto_fetch_value(mmu, next_byte, final_addr_mode);
					Byte old_carry = check_flag(CPU_FLAG_C);
					set_flag(CPU_FLAG_C, to_rotate & 1);
					to_rotate >>= 1;
					to_rotate |= old_carry << 7;
					set_flag(CPU_FLAG_Z, to_rotate == 0); // Documented incorrectly on NESdev?
					set_flag(CPU_FLAG_N, to_rotate & 0b10000000);
					// TODO: Figure out how this works if we are in accumulator addressing mode
					auto_write_value(mmu, next_byte, final_addr_mode, to_rotate);
					break;
				}
				case 0b100: {
					// STX - Store X Register
					// STX A = TXA but that is handled earlier

					// Addressing mode quirk:
					// zpx <-> zpy
					// abx <-> aby
					if (final_addr_mode == CPU_ADDR_MODE_ZPX)
						final_addr_mode = CPU_ADDR_MODE_ZPY;
					else if (final_addr_mode == CPU_ADDR_MODE_ZPY)
						final_addr_mode = CPU_ADDR_MODE_ZPX;

					// STX abs,Y is unassigned
					if (final_addr_mode != CPU_ADDR_MODE_INVALID) {
						auto_write_value(mmu, next_byte, final_addr_mode, X);
					}
					break;
				}
				case 0b101: {
					// LDX - Load X Register
					// LDX A = TAX but that is handled earlier

					// Addressing mode quirk:
					// zpx <-> zpy
					// abx <-> aby
					if (final_addr_mode == CPU_ADDR_MODE_ZPX)
						final_addr_mode = CPU_ADDR_MODE_ZPY;
					else if (final_addr_mode == CPU_ADDR_MODE_ZPY)
						final_addr_mode = CPU_ADDR_MODE_ZPX;
					else if (final_addr_mode == CPU_ADDR_MODE_ABX)
						final_addr_mode = CPU_ADDR_MODE_ABY;
					else if (final_addr_mode == CPU_ADDR_MODE_ABY)
						final_addr_mode = CPU_ADDR_MODE_ABX;
					
					X = auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_Z, X == 0);
					set_flag(CPU_FLAG_N, X & 0b10000000);
					break;
				}
				case 0b110: {
					// DEC - Decrement Memory
					// DEC A is DEX but that is handled earlier
					// TODO: Check if this uses the correct number of cycles
					Byte M = lo(static_cast<Word>(auto_fetch_value(mmu, next_byte, final_addr_mode) - 1));
					set_flag(CPU_FLAG_Z, M == 0);
					set_flag(CPU_FLAG_N, M & 0b10000000);
					auto_write_value(mmu, next_byte, final_addr_mode, M);
					break;
				}
				case 0b111: {
					// INC - Increment Memory
					// INC A is NOP but that is handled earlier
					// TODO: Check if this uses the correct number of cycles
					Byte M = lo(static_cast<Word>(auto_fetch_value(mmu, next_byte, final_addr_mode) + 1));
					set_flag(CPU_FLAG_Z, M == 0);
					set_flag(CPU_FLAG_N, M & 0b10000000);
					auto_write_value(mmu, next_byte, final_addr_mode, M);
					break;
				}
				default:
				PC++;
				return INVALID;
			}
			auto_increment_pc(final_addr_mode);
			break;
			case 0b00: // Group 3
			if (bbb == 0b100) {
				// Covers all conditional branch instructions
				Byte untranslated_flag = aaa >> 1;
				Byte condition = aaa & 1;
				Byte flag = 0;
				switch (untranslated_flag) {
					case 0: flag = CPU_FLAG_N; break;
					case 1: flag = CPU_FLAG_V; break;
					case 2: flag = CPU_FLAG_C; break;
					case 3: flag = CPU_FLAG_Z;
					default: break;
				}

				if (check_flag(flag) == condition) {
					last_jump_origin = PC;
					stall_n_cycles(mmu, 1); // TODO: 2 if to a new page
					PC = static_cast<Word>(PC + (Byte_S)next_byte); // Convert to signed type to do signed addition
				}
				
				PC += 2; // PC is always incremented by 2 here
				last_jump_target = PC;
				break; // Prevents the switch(aaa) from running
			}
			switch (aaa) {
				case 0b001: {
					// BIT - Bit Test
					Word address_to_test = widen(next_byte);
					if (bbb == 0b011) {
						// Absolute mode
						addr_bus_value = PC + 2;
						exec_cycle(mmu, CPU_UOP_FETCH); // Get second byte of address to test
						address_to_test |= static_cast<Word>(widen(data_bus_value) << 8);
						PC++;
					}
					
					addr_bus_value = address_to_test;
					exec_cycle(mmu, CPU_UOP_FETCH);
					Byte result = A & data_bus_value;

					set_flag(CPU_FLAG_Z, result == 0);
					set_flag(CPU_FLAG_V, data_bus_value & 0b01000000);
					set_flag(CPU_FLAG_N, data_bus_value & 0b10000000);

					PC += 2;
					break;
				}
				// llx.com gets these two backwards
				case 0b010: {
					// JMP - Absolute Jump
					Word jump_target = make_address(next_byte, fetch_one_byte(mmu, PC + 2));
					last_jump_origin = PC;
					last_jump_target = jump_target;
					PC = jump_target;
					break;
				}
				case 0b011: {
					// JMP - Indirect Jump
					Byte jump_target_location_lo = next_byte;
					Byte jump_target_location_hi = fetch_one_byte(mmu, PC + 2);
					Word jump_target_location = make_address(jump_target_location_lo, jump_target_location_hi);
					bool wraparound = jump_target_location_lo == 0xFF;

					Byte jump_target_lo = fetch_one_byte(mmu, jump_target_location);
					Byte jump_target_hi = fetch_one_byte(mmu, wraparound ? jump_target_location + 1 - 0x100 : jump_target_location + 1);
					Word jump_target = make_address(jump_target_lo, jump_target_hi);
					last_jump_origin = PC;
					last_jump_target = jump_target;
					PC = jump_target;
					break;
				}
				case 0b100: {
					// STY - Store Y Register
					auto_write_value(mmu, next_byte, final_addr_mode, Y);
					auto_increment_pc(final_addr_mode);
					break;
				}
				case 0b101: {
					// LDY - Load Y Register
					Y = auto_fetch_value(mmu, next_byte, final_addr_mode);
					set_flag(CPU_FLAG_Z, Y == 0);
					set_flag(CPU_FLAG_N, Y & 0b10000000);
					auto_increment_pc(final_addr_mode);
					break;
				}
				case 0b110: {
					// CPY - Compare Y Register
					Byte compare_mem = auto_fetch_value(mmu, next_byte, final_addr_mode);
					Word result = static_cast<Word>(Y - compare_mem);
					
					set_flag(CPU_FLAG_C, Y >= compare_mem);
					set_flag(CPU_FLAG_Z, result == 0);
					set_flag(CPU_FLAG_N, static_cast<Byte>(result & 0b10000000));
					auto_increment_pc(final_addr_mode);
					break;
				}
				case 0b111: {
					// CPX - Compare X Register
					Byte compare_mem = auto_fetch_value(mmu, next_byte, final_addr_mode);
					Word result = static_cast<Word>(X - compare_mem);
					
					set_flag(CPU_FLAG_C, X >= compare_mem);
					set_flag(CPU_FLAG_Z, result == 0);
					set_flag(CPU_FLAG_N, static_cast<Byte>(result & 0b10000000));
					auto_increment_pc(final_addr_mode);
					break;
				}
				default:
				PC++;
				return INVALID;
			}
			break;
			default:
			PC++;
			return INVALID;
		}

		last_good_instruction = old_pc;
		if (PC == old_pc) return HALT;
		return CONTINUE;
	}
};

int main(int argc, char* argv[]) {
	CPU cpu;
	MMU mmu;
	mmu.initialize();

	if (argc > 1) {
		std::cout << "Attempting to load ROM: " << argv[1] << std::endl;
		std::ifstream rom_file(argv[1], std::ios::binary);
		if (!rom_file) {
			std::cerr << "Error: Could not open ROM file." << std::endl;
			return 1;
		}

		char byte;
		Word address = 0x0000;
		while (rom_file.get(byte)) {
			mmu.write_byte(address++, static_cast<Byte>(byte));
		}
		rom_file.close();
	} else {
		std::cout << "No ROM provided." << std::endl;
	}
	
	cpu.reset(mmu);
	cpu.dump_state(mmu);
	
	std::string input;
	std::string logfile;
	std::ofstream logfile_stream;
	bool logging = false;
	bool running = true;
	bool paused = true;
	
	std::cout << "\nPress Enter to execute next instruction or 'q' to quit\n";
	
	while (running) {
		bool bypass_breakpoints = false;
		if (paused) {
			std::getline(std::cin, input);
			std::vector<std::string> command_parts;
			char cmd = ' ';

			if (input.length() > 0) {
				std::istringstream splitter(input);
				std::string current;

				while (getline(splitter, current, ' ')) {
					command_parts.push_back(current);
				}

				cmd = command_parts[0][0];
			}
			else {
				bypass_breakpoints = true;
			}
			
			if (cmd == 'q' || cmd == 'Q') {
				std::cout << "Quitting emulator...\n";
				running = false;
				break;
			}
			else if (cmd == 't' || cmd == 'T') {
				if (command_parts.size() > 1) {
					std::string type_name = command_parts[1];
					bool found = true;
					if (type_name == "MOS") {
						cpu.type = MOS;
					}
					else if (type_name == "NES") {
						cpu.type = NES;
					}
					else {
						std::cout << "Unknown type." << std::endl;
						found = false;
					}

					if (found) {
						std::cout << "Successfully switched 6502 type." << std::endl;
					}
				}
				else {
					std::cout << "Specify the type of 6502." << std::endl;
				}
				continue;
			}
			else if (cmd == 'l' || cmd == 'L') {
				if (command_parts.size() > 1) {
					logfile = command_parts[1];
					logging = true;
					std::cout << "Logging to '" << logfile << "'" << std::endl;
				}
				else {
					std::cout << "Please specify a file path to log to." << std::endl;
				}
				continue;
			}
			else if (cmd == 'j' || cmd == 'J') {
				try {
					Word location = static_cast<Word>(parse_numeric_literal(command_parts[1]));
					std::cout << "Jumping to 0x" << std::hex << (int)location << std::endl;
					cpu.PC = location;
				}
				catch (const std::exception& e) {
					std::cerr << "Invalid numeric input: " << e.what() << std::endl;
				}
				continue;
			}
			else if (cmd == 'b' || cmd == 'B') {
				try {
					Word location = static_cast<Word>(parse_numeric_literal(command_parts[1]));
					std::cout << "Breakpoint set at 0x" << std::hex << (int)location << std::endl;
					cpu.breakpoints.push_back(location);
				}
				catch (const std::exception& e) {
					std::cerr << "Invalid numeric input: " << e.what() << std::endl;
				}
				continue;
			}
			else if (cmd == 'r' || cmd == 'R') {
				std::cout << "Running..." << std::endl;
				paused = false;
			}
			else if (cmd == 'i' || cmd == 'I') {
				if (command_parts.size() > 1) {
					try {
						Word location = static_cast<Word>(parse_numeric_literal(command_parts[1]));
						std::cout << "Value at 0x" << std::hex << (int)location 
							<< " is 0x" << std::hex << (int)mmu.read_byte(location) << std::endl;
					}
					catch (const std::exception& e) {
						std::cerr << "Invalid numeric input: " << e.what() << std::endl;
					}
				}
				else {
					cpu.dump_state(mmu);
				}
				continue;
			}
			else {
				std::cout << "Stepping one instruction." << std::endl;
				bypass_breakpoints = true;
			}
		}

		if (logging) {
			if (!logfile_stream.is_open()) {
				logfile_stream.open(logfile);
			}

			logfile_stream << cpu.log_state(mmu) << std::endl;
		}

		CPUStatus status = cpu.exec_instruction(mmu, bypass_breakpoints);

		if (status == HALT) {
			cpu.dump_state(mmu);
			std::cout << "A halt was detected!" << std::endl;
			paused = true;
		}
		else if (status == INVALID) {
			cpu.dump_state(mmu);
			std::cout << "The CPU encountered an invalid instruction!" << std::endl
				<< "Execution may be resumed, but unexpected behavior could occur." << std::endl;
			paused = true;
		}
		else if (status == BREAKPOINT) {
			cpu.dump_state(mmu);
			std::cout << "Breakpoint hit!" << std::endl;
			paused = true;
		}
	}

	logfile_stream.close();

	return 0;
}
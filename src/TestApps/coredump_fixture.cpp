/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Generates a synthetic SerenityOS-format coredump for the CrashReporter test.
// The byte layout mirrors Kernel/Tasks/Coredump.cpp: an ET_CORE ELF file with
// PT_LOAD segments backing each region and one PT_NOTE segment whose payload is
// a single "SerenityOS" note wrapping the packed Serenity entries (ProcessInfo,
// ThreadInfo, MemoryRegionInfo..., Metadata, Null). The reference binary (the
// debuggee fixture) provides real symbol addresses, so CrashReporter's
// backtrace resolves against an actual ELF on disk.

#include <AK/Types.h>
#include <AK/Vector.h>
#include <LibCore/MappedFile.h>
#include <LibELF/Core.h>
#include <LibELF/ELFABI.h>
#include <stdio.h>
#include <string.h>

static void append_bytes(Vector<u8>& out, void const* data, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        out.append(static_cast<u8>(reinterpret_cast<u8 const*>(data)[i]));
}

static FlatPtr find_symbol(ReadonlyBytes bytes, char const* name)
{
    auto const& ehdr = *bit_cast<Elf64_Ehdr const*>(bytes.data());
    auto const* shdrs = bit_cast<Elf64_Shdr const*>(bytes.data() + ehdr.e_shoff);

    int symtab_index = -1;
    int strtab_index = -1;
    char const* section_names = reinterpret_cast<char const*>(bytes.data() + shdrs[ehdr.e_shstrndx].sh_offset);
    for (int i = 0; i < ehdr.e_shnum; ++i) {
        StringView section_name { section_names + shdrs[i].sh_name, strlen(section_names + shdrs[i].sh_name) };
        if (section_name == ".symtab"sv)
            symtab_index = i;
        else if (section_name == ".strtab"sv)
            strtab_index = i;
    }
    if (symtab_index < 0 || strtab_index < 0)
        return 0;

    auto const& symtab = shdrs[symtab_index];
    auto const& strtab = shdrs[strtab_index];
    size_t entry_count = symtab.sh_size / sizeof(Elf64_Sym);
    for (size_t i = 0; i < entry_count; ++i) {
        auto const& symbol = reinterpret_cast<Elf64_Sym const*>(bytes.data() + symtab.sh_offset)[i];
        if (strcmp(reinterpret_cast<char const*>(bytes.data() + strtab.sh_offset) + symbol.st_name, name) == 0)
            return static_cast<FlatPtr>(symbol.st_value);
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <output.core> <reference-binary>\n", argv[0]);
        return 1;
    }

    auto mapped = Core::MappedFile::map(StringView { argv[2], strlen(argv[2]) });
    if (mapped.is_error()) {
        fprintf(stderr, "could not map reference binary %s\n", argv[2]);
        return 1;
    }
    ReadonlyBytes ref_bytes = mapped.value()->bytes();

    // The fixture functions are C++ symbols, so they carry the Itanium mangling.
    FlatPtr inner_crash = find_symbol(ref_bytes, "_Z11inner_crashv");
    FlatPtr main_address = find_symbol(ref_bytes, "main");
    if (!inner_crash || !main_address) {
        fprintf(stderr, "reference binary has no usable .symtab\n");
        return 1;
    }

    // Mirror every PT_LOAD of the reference binary so each symbol address falls
    // inside some region (code usually lives in a later segment than the first).
    // Their data is all zeros: backtrace symbolication reads the real file from
    // disk, not the coredump body.
    auto const& ref_ehdr = *bit_cast<Elf64_Ehdr const*>(ref_bytes.data());
    auto const* ref_phdrs = bit_cast<Elf64_Phdr const*>(ref_bytes.data() + ref_ehdr.e_phoff);

    struct ExeSegment {
        FlatPtr start;
        size_t size;
    };
    Vector<ExeSegment> exe_segments;
    for (int i = 0; i < ref_ehdr.e_phnum; ++i) {
        if (ref_phdrs[i].p_type == PT_LOAD)
            exe_segments.append({ static_cast<FlatPtr>(ref_phdrs[i].p_vaddr), static_cast<size_t>(ref_phdrs[i].p_filesz) });
    }
    if (exe_segments.is_empty()) {
        fprintf(stderr, "reference binary has no PT_LOAD segment\n");
        return 1;
    }

    static constexpr FlatPtr stack_base = 0x10000000;
    static constexpr size_t stack_size = 0x1000;

    size_t phdr_count = exe_segments.size() + 2; // executable segments + stack + notes
    size_t data_offset = sizeof(Elf64_Ehdr) + phdr_count * sizeof(Elf64_Phdr);

    Elf64_Ehdr ehdr {};
    ehdr.e_ident[0] = 0x7f;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; // ELFCLASS64
    ehdr.e_ident[5] = 1; // ELFDATA2LSB
    ehdr.e_ident[6] = 1; // EV_CURRENT
    ehdr.e_type = ET_CORE;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = 1;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = static_cast<decltype(ehdr.e_phnum)>(phdr_count);
    // The kernel coredump carries no section table, but validate_elf_header()
    // still expects a well-formed (empty) one.
    ehdr.e_shoff = 0;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = 0;

    Vector<Elf64_Phdr> phdrs;
    size_t offset = data_offset;
    for (auto& segment : exe_segments) {
        Elf64_Phdr phdr {};
        phdr.p_type = PT_LOAD;
        phdr.p_flags = PF_R | PF_X;
        phdr.p_offset = offset;
        phdr.p_vaddr = segment.start;
        phdr.p_filesz = segment.size;
        phdr.p_memsz = segment.size;
        phdrs.append(phdr);
        offset += segment.size;
    }

    size_t stack_phdr_index = phdrs.size();
    Elf64_Phdr stack_phdr {};
    stack_phdr.p_type = PT_LOAD;
    stack_phdr.p_flags = PF_R;
    stack_phdr.p_offset = offset;
    stack_phdr.p_vaddr = stack_base;
    stack_phdr.p_filesz = stack_size;
    stack_phdr.p_memsz = stack_size;
    phdrs.append(stack_phdr);
    offset += stack_size;

    // Notes segment: one Elf_Note header ("SerenityOS\0", 4-byte aligned) then
    // the packed Serenity entries, exactly as the kernel writes them.
    Vector<u8> notes;
    // The kernel uses "SerenityOS\0"sv, whose length is 11: the name bytes
    // include one explicit NUL (the literal's implicit terminator is not part
    // of the StringView).
    static constexpr StringView note_name = "SerenityOS\0"sv;
    Elf_Note note {};
    note.namesz = static_cast<decltype(note.namesz)>(note_name.length());
    note.type = 0;
    append_bytes(notes, &note, sizeof(note));
    append_bytes(notes, note_name.characters_without_null_termination(), note_name.length());
    for (size_t i = 0; i < mod(-static_cast<int>(note.namesz), 4); ++i)
        notes.append(0);

    size_t desc_start = notes.size();

    ELF::Core::NotesEntryHeader process_header {};
    process_header.type = ELF::Core::NotesEntryHeader::Type::ProcessInfo;
    append_bytes(notes, &process_header, sizeof(process_header));
    char process_json[1024];
    snprintf(process_json, sizeof(process_json),
        "{\"pid\":42,\"termination_signal\":11,\"executable_path\":\"%s\","
        "\"arguments\":[\"crashy-fixture\"],\"environment\":[\"PATH=/usr/bin\"]}",
        argv[2]);
    append_bytes(notes, process_json, strlen(process_json));
    notes.append(0);

    ELF::Core::ThreadInfo thread {};
    thread.header.type = ELF::Core::NotesEntryHeader::Type::ThreadInfo;
    thread.tid = 1;
    thread.regs.rip = inner_crash;
    thread.regs.rbp = stack_base + 0x100;
    thread.regs.rsp = stack_base + 0x80;
    append_bytes(notes, &thread, sizeof(thread));

    for (size_t i = 0; i < exe_segments.size(); ++i) {
        ELF::Core::MemoryRegionInfo exe_region {};
        exe_region.header.type = ELF::Core::NotesEntryHeader::Type::MemoryRegionInfo;
        exe_region.region_start = exe_segments[i].start;
        exe_region.region_end = exe_segments[i].start + exe_segments[i].size;
        exe_region.program_header_index = static_cast<decltype(exe_region.program_header_index)>(i);
        append_bytes(notes, &exe_region, sizeof(exe_region));
        char exe_name[1024];
        snprintf(exe_name, sizeof(exe_name), "%s: segment %zu", argv[2], i);
        append_bytes(notes, exe_name, strlen(exe_name));
        notes.append(0);
    }

    ELF::Core::MemoryRegionInfo stack_region {};
    stack_region.header.type = ELF::Core::NotesEntryHeader::Type::MemoryRegionInfo;
    stack_region.region_start = stack_base;
    stack_region.region_end = stack_base + stack_size;
    stack_region.program_header_index = static_cast<decltype(stack_region.program_header_index)>(stack_phdr_index);
    append_bytes(notes, &stack_region, sizeof(stack_region));
    append_bytes(notes, "stack", strlen("stack"));
    notes.append(0);

    ELF::Core::Metadata metadata {};
    metadata.header.type = ELF::Core::NotesEntryHeader::Type::Metadata;
    append_bytes(notes, &metadata, sizeof(metadata));
    append_bytes(notes, "{\"assertion\":\"false == true in inner_crash()\"}", strlen("{\"assertion\":\"false == true in inner_crash()\"}"));
    notes.append(0);

    ELF::Core::NotesEntryHeader null_entry {};
    null_entry.type = ELF::Core::NotesEntryHeader::Type::Null;
    append_bytes(notes, &null_entry, sizeof(null_entry));

    size_t desc_size = notes.size() - desc_start;
    // Patch descsz into the note header (little-endian u32 at offset 4).
    notes[4] = static_cast<u8>(desc_size & 0xff);
    notes[5] = static_cast<u8>((desc_size >> 8) & 0xff);
    notes[6] = static_cast<u8>((desc_size >> 16) & 0xff);
    notes[7] = static_cast<u8>((desc_size >> 24) & 0xff);
    for (size_t i = 0; i < mod(-static_cast<int>(desc_size), 4); ++i)
        notes.append(0);

    Elf64_Phdr note_phdr {};
    note_phdr.p_type = PT_NOTE;
    note_phdr.p_offset = offset;
    note_phdr.p_filesz = notes.size();
    note_phdr.p_memsz = notes.size();
    phdrs.append(note_phdr);

    // Fake stack with one frame record: [previous_fp, return_address] pairs at
    // 0x100 and 0x200; the zero return address terminates the unwind.
    Vector<u8> stack_data;
    stack_data.resize(stack_size);
    auto write_u64 = [&](size_t offset_, u64 value) {
        for (int i = 0; i < 8; ++i)
            stack_data[offset_ + i] = static_cast<u8>((value >> (i * 8)) & 0xff);
    };
    write_u64(0x100, stack_base + 0x200); // previous frame pointer
    write_u64(0x108, main_address + 8);   // return address inside main()
    write_u64(0x200, 0);                  // end of the chain
    write_u64(0x208, 0);

    FILE* out = fopen(argv[1], "wb");
    if (!out) {
        fprintf(stderr, "could not open %s for writing\n", argv[1]);
        return 1;
    }
    fwrite(&ehdr, sizeof(ehdr), 1, out);
    fwrite(phdrs.data(), phdrs.size() * sizeof(Elf64_Phdr), 1, out);
    Vector<u8> zero_data;
    for (auto& segment : exe_segments) {
        zero_data.resize(segment.size);
        fwrite(zero_data.data(), segment.size, 1, out);
    }
    fwrite(stack_data.data(), stack_size, 1, out);
    fwrite(notes.data(), notes.size(), 1, out);
    fclose(out);

    size_t exe_total = 0;
    for (auto& segment : exe_segments)
        exe_total += segment.size;
    printf("wrote %zu bytes to %s\n", sizeof(ehdr) + phdrs.size() * sizeof(Elf64_Phdr) + exe_total + stack_size + notes.size(), argv[1]);
    return 0;
}

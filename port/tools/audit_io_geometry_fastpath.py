#!/usr/bin/env python3
"""Prove an isolated geometry-write dispatch candidate without editing I/O.

This is a host-side semantic dispatch/mirror test, not a GPU or timing oracle.
The unmodified io_write body is extracted into the test and paired with a
candidate whose only difference is an early aligned 32-bit geometry path.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

PRELUDE = r'''
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <tuple>
#include <vector>
using Event = std::tuple<unsigned,uint32_t,uint32_t>;
alignas(8) static std::array<uint8_t,0x2000> mirror;
static std::vector<Event> events;
static bool init_ok;
static unsigned bank_queries;
static uint8_t* g_io;
struct Fatal {};
[[noreturn]] static void audit_abort(){throw Fatal{};}
struct Banks { uint8_t map(unsigned b,uint8_t v){events.emplace_back(8,b,v);return v;} } banks;
static Banks* active_banks;
static Banks* io_vram_banks(){++bank_queries;return active_banks;}
static bool io_init(){if(init_ok)g_io=mirror.data();return init_ok;}
static bool vram_cpu_address(uint32_t address){return address>>24==6;}
static void vram_cpu_write(uint32_t a,uint32_t v,unsigned w){events.emplace_back(4,a,v^w);}
struct Capture {
 uint32_t value=0;
 void write_control(unsigned offset,uint32_t incoming,unsigned width){
  const uint32_t mask=width==4?0xffffffffu:((1u<<(width*8))-1u)<<(offset*8);
  value=(value&~mask)|((incoming<<(offset*8))&mask);
  events.emplace_back(3,offset,incoming);
 }
 uint32_t control(){return value;}
} physical_capture;
static uint64_t raw_read(uint32_t a,unsigned w){uint64_t v=0;std::memcpy(&v,mirror.data()+(a&0x1fff),w);return v;}
static void raw_write(uint32_t a,uint64_t v,unsigned w){std::memcpy(mirror.data()+(a&0x1fff),&v,w);}
static void rt_dispatch_pending_irqs(){events.emplace_back(5,0,0);}
static void gx_write_fifo(uint32_t v){events.emplace_back(1,0x04000400,v);}
static void gx_write_port(uint32_t a,uint32_t v){events.emplace_back(2,a,v);}
static void run_divide(){events.emplace_back(6,0,0);}
static void run_sqrt(){events.emplace_back(7,0,0);}
constexpr uint32_t DIV_DENOM=0x04000298,DIV_NUMER=0x04000290,DIVCNT=0x04000280;
constexpr uint32_t SQRT_PARAM=0x040002b8,SQRTCNT=0x040002b0;
'''

TEST = r'''
int main(){
 const uint64_t values[]={0,1,0xffffffffull,0x80000000ull,0x7fffffffull,
  0x123456789abcdef0ull,0xffffffffffffffffull,0x8000000000000000ull,
  0x0102030405060708ull,0xaaaaaaaa55555555ull,0x00000001ffffffffull,
  0xffffffff00000000ull};
 const unsigned widths[]={1,2,4,8};
 std::vector<uint32_t> addresses;
 for(uint32_t a=0x04000000;a<=0x04000620;++a)addresses.push_back(a);
 for(uint32_t a=0x04001000;a<=0x04001008;++a)addresses.push_back(a);
 for(uint32_t base:{0x02000000u,0x023fffa8u,0x027fffa8u,0x05000000u,
                   0x06000000u,0x06800000u,0x06880000u,0x07000000u})
  for(unsigned offset=0;offset<8;++offset)addresses.push_back(base+offset);
 uint64_t checked=0,fast=0,fallback=0,query_saved=0;
 for(unsigned bank=0;bank<2;++bank)for(unsigned init=0;init<3;++init)
 for(uint32_t a:addresses)for(unsigned w:widths)for(uint64_t v:values){
  std::array<uint8_t,0x2000> seed;
  for(unsigned i=0;i<seed.size();++i)seed[i]=uint8_t(i*37+11);
  auto reset=[&](){mirror=seed;events.clear();bank_queries=0;
   physical_capture.value=0xabcdef01;active_banks=bank?&banks:nullptr;init_ok=init!=2;g_io=init==0?mirror.data():nullptr;};
  reset();bool expected_fatal=false;try{baseline_io_write(a,v,w);}catch(const Fatal&){expected_fatal=true;}
  const auto expected=mirror;const auto expected_events=events;const auto expected_capture=physical_capture.value;const auto q=bank_queries;const bool ready=g_io!=nullptr;
  reset();bool actual_fatal=false;try{candidate_io_write(a,v,w);}catch(const Fatal&){actual_fatal=true;}
  if(mirror!=expected||events!=expected_events||physical_capture.value!=expected_capture||actual_fatal!=expected_fatal||(g_io!=nullptr)!=ready){
   std::printf("FAIL address=%08x width=%u value=%016llx bank=%u init=%u\n",a,w,(unsigned long long)v,bank,init);return 1;
  }
  const bool eligible=w==4&&(a==0x04000400||(a>=0x04000440&&a<=0x040005fc&&(a&3)==0));
  if(eligible&&ready){++fast;query_saved+=q-bank_queries;}else ++fallback;
  ++checked;
 }
 std::printf("{\"cases\":%llu,\"fast_cases\":%llu,\"fallback_cases\":%llu,\"bank_queries_avoided\":%llu,\"mirror_dispatch_init_exact\":true}\n",
  (unsigned long long)checked,(unsigned long long)fast,(unsigned long long)fallback,(unsigned long long)query_saved);
}
'''


def extract_function(source):
    start = source.index('void io_write(uint32_t addr, uint64_t value, unsigned width) {')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--source', type=Path, help='Frozen pre-edit I/O source')
    parser.add_argument('--candidate-source', type=Path, help='Verify the actual edited source, with no other code changes in io_write')
    parser.add_argument('--policy-source', type=Path, help='Verify a diagnostic candidate with one global startup-only policy switch, in both modes')
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[1]
    source_path = args.source or port/'ntr/io.cpp'
    source = source_path.read_text()
    body = extract_function(source)
    anchor = '    if (!g_io && !io_init()) return;'
    if body.count(anchor) != 1:
        raise ValueError('I/O initialization boundary changed')
    prefix = '''
    if (width == 4 && (addr == 0x04000400u ||
        (addr >= 0x04000440u && addr <= 0x040005fcu && (addr & 3u) == 0))) {
        // Keep the exact register mirror write and original dispatch order.
        // Constant width lets the compiler inline the four-byte copy.
        raw_write(addr, static_cast<uint32_t>(value), 4);
        if (addr == 0x04000400u) gx_write_fifo(static_cast<uint32_t>(value));
        else gx_write_port(addr, static_cast<uint32_t>(value));
        return;
    }
'''
    candidate = body.replace(anchor, anchor+prefix)
    if args.candidate_source and args.policy_source:
        parser.error('Choose candidate-source or policy-source')
    actual_path = args.candidate_source or args.policy_source
    if actual_path:
        actual = extract_function(actual_path.read_text())
        expected = candidate
        if args.policy_source:
            expected = expected.replace('    if (width == 4 &&', '    if (sm64ds_geometry_fastpath_diagnostic_enabled && width == 4 &&', 1)
        def normalized(text):
            return re.sub(r'\s+', '', re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', text))
        if normalized(actual) != normalized(expected):
            raise ValueError('Actual candidate differs from the proven one outside comments/whitespace')
        candidate = actual
    elif 'addr <= 0x040005fcu' in body:
        raise ValueError('Source already contains the fast path; pass a frozen --source baseline')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out/'io-original.cpp').write_text(source)
    (out/'io-write-candidate.cpp').write_text(candidate+'\n')
    # Observe the non-returning IPC failure as a caught sentinel, in both
    # versions. It remains a real std::abort in the archived production body.
    prelude = PRELUDE
    tests = TEST
    if args.policy_source:
        prelude += '\nbool sm64ds_geometry_fastpath_diagnostic_enabled=true;\n'
        tests = tests.replace(' for(unsigned bank=0;', ' for(bool policy:{false,true}) for(unsigned bank=0;', 1)
        tests = tests.replace('mirror=seed;events.clear();bank_queries=0;', 'mirror=seed;events.clear();bank_queries=0;sm64ds_geometry_fastpath_diagnostic_enabled=policy;', 1)
        tests = tests.replace('if(eligible&&ready)', 'if(policy&&eligible&&ready)', 1)
    test = prelude+body.replace('void io_write(', 'void baseline_io_write(').replace('std::abort()', 'audit_abort()')+'\n'+candidate.replace('void io_write(', 'void candidate_io_write(').replace('std::abort()', 'audit_abort()')+'\n'+tests
    test_path = out/'dispatch-mirror-test.cpp'
    test_path.write_text(test)
    binary = out/'dispatch-mirror-test'
    command = [args.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-fsanitize=address,undefined', str(test_path), '-o', str(binary)]
    compile_run = subprocess.run(command, capture_output=True, text=True)
    (out/'compile.log').write_text(compile_run.stdout+compile_run.stderr)
    compile_run.check_returncode()
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    (out/'run.log').write_text(run.stdout+run.stderr)
    run.check_returncode()
    result = json.loads(run.stdout)
    result.update(scope='Dispatch, mirror and lazy initialization under abstract callbacks; not GX execution or menu speed',
        tool_modifies_production_source=False, actual_candidate_verified=bool(actual_path), source_path=str(source_path),
        candidate_source=str(actual_path) if actual_path else None,
        policy_modes_tested=[False,True] if args.policy_source else None,
        source_sha256=hashlib.sha256(source.encode()).hexdigest(),
        candidate_sha256=hashlib.sha256(candidate.encode()).hexdigest(),
        compiler_command=command, sanitizer='AddressSanitizer and UndefinedBehaviorSanitizer',
        performance_gain='NOT_MEASURED',
        domain='Every byte address 04000000..04000620 and 04001000..04001008; eight-byte windows for main RAM/shared mirrors/palette/VRAM/OAM; widths 1/2/4/8; twelve 64-bit values; physical bus attached/detached; ready/lazy-init-success/lazy-init-failure')
    (out/'dispatch-mirror-proof.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build and run every Vanguard OT Range host unit-test suite (pure sim/net logic, no ESP deps).
# Run from this directory:   ./run_all.sh
# Needs a host g++ with -std=c++17. Exits non-zero if any suite fails to build or run.
#
# Each suite is compiled against the sim/ + net/ sources it includes (see the dependency map below).
# Totals as of this writing: 13 suites, 300 checks, 0 failures.
set -u
cd "$(dirname "$0")"
INC="-I../source/ot -I../source/ui -I../source"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# suite -> extra source files (relative to ../source/ot)
declare -A DEPS=(
  [ot_modbus_tests]="net/Modbus.cpp"
  [ot_plant_tests]="sim/PlantModel.cpp"
  [ot_pump_tests]="sim/PumpSystem.cpp"
  [ot_pumpproto_tests]="net/PumpProto.cpp sim/PumpSystem.cpp"
  [ot_sis_tests]="sim/SafetySystem.cpp"
  [ot_tristation_tests]="net/Tristation.cpp sim/SafetySystem.cpp"
  [ot_substation_tests]="sim/Substation.cpp"
  [ot_iec104_tests]="net/Iec104.cpp sim/Substation.cpp"
  [ot_opcua_tests]="sim/OpcUaServer.cpp net/OpcUaProto.cpp"
  [ot_histsqlunion_tests]="sim/HistSqlUnion.cpp"
  [ot_toolkitzip_tests]="net/ToolkitZip.cpp"
  [ot_dam_tests]="net/Modem.cpp sim/DamSystem.cpp"
  [ot_goose_tests]="sim/GooseSim.cpp net/Goose.cpp"
  [ot_defensive_tests]="sim/Defensive.cpp"
  [ot_session_tests]="../ui/SessionCodec.cpp"
  [ot_snmp_tests]="sim/SnmpDevice.cpp net/Snmp.cpp"
  [ot_catalogue_tests]=""
  [ot_badge_tests]=""
)

pass=0; fail=0; total=0
for suite in $(printf '%s\n' "${!DEPS[@]}" | sort); do
  srcs=""
  for s in ${DEPS[$suite]}; do srcs="$srcs ../source/ot/$s"; done
  if ! g++ -std=c++17 $INC "$suite.cpp" $srcs -o "$OUT/$suite" 2>"$OUT/$suite.err"; then
    echo "BUILD-FAIL $suite"; tail -3 "$OUT/$suite.err"; fail=$((fail+1)); continue
  fi
  out=$("$OUT/$suite" 2>&1); rc=$?
  n=$(printf '%s\n' "$out" | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+' | head -1)
  if [ $rc -eq 0 ]; then
    echo "OK   $suite: $(printf '%s' "$out" | tail -1)"; pass=$((pass+1)); total=$((total+${n:-0}))
  else
    echo "FAIL $suite: $(printf '%s' "$out" | tail -1)"; fail=$((fail+1))
  fi
done

echo "-----------------------------------------------"
echo "suites: $pass passed, $fail failed ; total checks: $total"
[ $fail -eq 0 ]

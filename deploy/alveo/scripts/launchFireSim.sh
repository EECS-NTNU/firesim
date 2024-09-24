#!/usr/bin/env bash

RUN_DIR=$(pwd)
SCRIPT_DIR=$(dirname $(readlink -f $0))

FPGA=$1
WORKLOAD=$2

if [ -z ${PLATFORM+x} ]; then
	echo "Variable PLATFORM needs to be set!" >&2
	exit 1
fi

if [ -z ${TARGET_CONFIG+x} ]; then
	echo "Variable TARGET_CONFIG needs to be set!" >&2
	exit 1
fi

if [ -z ${PLATFORM_CONFIG+x} ]; then
	echo "Variable PLATFORM_CONFIG needs to be set!" >&2
	exit 1
fi

#ALVEO_PLATFORM=$(fpga-util.py -l | awk "NR>1 && \$2 == \"${FPGA}\"{print \$4; exit}")
#ALVEO_PLATFORM="u250"

DRIVER="${SCRIPT_DIR}/../../../sim/output/${PLATFORM}/FireSim-${TARGET_CONFIG}-${PLATFORM_CONFIG}/FireSim-alveo"
RUNTIME_CONFIG="${SCRIPT_DIR}/../../../sim/output/${PLATFORM}/FireSim-${TARGET_CONFIG}-${PLATFORM_CONFIG}/runtime.conf"
BITSTREAM="${SCRIPT_DIR}/../../../sim/generated-src/${PLATFORM}/FireSim-${TARGET_CONFIG}-${PLATFORM_CONFIG}/${ALVEO_PLATFORM}/vivado_proj/firesim.bit"

# Different platforms might be implemented with different frequencies, lets try to find a frequency that matches
if [ ! -e "${DRIVER}" ] || [ ! -e "${RUNTIME_CONFIG}" ] || [ ! -e "${BITSTREAM}" ]; then
	for f in $(ls "${SCRIPT_DIR}/../../../sim/generated-src/${PLATFORM}/FireSim-${TARGET_CONFIG}-"* | grep -oE 'F[1-9][0-9]+MHz' | sort -rn); do
		NPLATFORM_CONFIG=$(echo "${PLATFORM_CONFIG}" | sed -E "s/F[1-9][0-9]Mhz/${f}/")
		NDRIVER="${SCRIPT_DIR}/../../../sim/output/${PLATFORM}/FireSim-${TARGET_CONFIG}-${NPLATFORM_CONFIG}/FireSim-alveo"
		NRUNTIME_CONFIG="${SCRIPT_DIR}/../../../sim/output/${PLATFORM}/FireSim-${TARGET_CONFIG}-${NPLATFORM_CONFIG}/runtime.conf"
		NBITSTREAM="${SCRIPT_DIR}/../../../sim/generated-src/${PLATFORM}/FireSim-${TARGET_CONFIG}-${NPLATFORM_CONFIG}/${ALVEO_PLATFORM}/vivado_proj/firesim.bit"
		if [ -e "${DRIVER}" ] && [ -e "${RUNTIME_CONFIG}" ] && [ -e "${BITSTREAM}" ]; then
			DRIVER=${NDRIVER}
			RUNTIME_CONFIG=${NRUNTIME_CONFIG}
			BITSTREAM=${NBITSTREAM}
			break
		fi
	done
fi

IMAGE_IMG="${RUN_DIR}/workload.img"
IMAGE_BIN="${RUN_DIR}/workload.bin"
IMAGE_RUNTIME_CONFIG="${RUN_DIR}/runtime.conf"

FAIL=0

for f in "${DRIVER}" "${BITSTREAM}" "${RUNTIME_CONFIG}" "${IMAGE_IMG}" "${IMAGE_BIN}"; do
	if [ ! -e "${f}" ]; then
		echo "Could not find file ${f}" >&2
		FAIL=1
	fi
done

if [ $FAIL -ne 0 ]; then
	exit $FAIL
fi

fpga-util.py -f "${FPGA}" -b "${BITSTREAM}"
#FPGA_ID=$(fpga-util.py -l | tail -n +2 | grep -F "${FPGA}" | awk '{print $3}')
FPGA_ID=$(fpga-util.py -l | tail -n +2 | grep -F "${FPGA}" | awk '{print $2}')

echo "+permissive" > "${IMAGE_RUNTIME_CONFIG}"

cat "${RUNTIME_CONFIG}" >> "${IMAGE_RUNTIME_CONFIG}"

if [ ! -z ${CUSTOM_RUNTIME_CONFIG+x} ]; then
	if [ -e "${CUSTOM_RUNTIME_CONFIG}" ]; then
		cat "${CUSTOM_RUNTIME_CONFIG}" >> "${IMAGE_RUNTIME_CONFIG}"
	elif [ -e "${SCRIPT_DIR}/../${CUSTOM_RUNTIME_CONFIG}" ]; then
		cat "${SCRIPT_DIR}/../${CUSTOM_RUNTIME_CONFIG}" >> "${IMAGE_RUNTIME_CONFIG}"
	fi
fi

set -x

echo "+slotid=${FPGA_ID}" >> "${IMAGE_RUNTIME_CONFIG}"
echo "+blkdev0=${IMAGE_IMG}" >> "${IMAGE_RUNTIME_CONFIG}"
echo "+permissive-off" >> "${IMAGE_RUNTIME_CONFIG}"
echo "${IMAGE_BIN}" >> "${IMAGE_RUNTIME_CONFIG}"

sync
xargs -t -a "${IMAGE_RUNTIME_CONFIG}" -d $'\n' "${DRIVER}"
sync

exit 0

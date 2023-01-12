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

DRIVER="${SCRIPT_DIR}/../../../sim/output/${PLATFORM}/FireSim-${TARGET_CONFIG}-${PLATFORM_CONFIG}/FireSim-alveo"
RUNTIME_CONFIG="${SCRIPT_DIR}/../../../sim/output/${PLATFORM}/FireSim-${TARGET_CONFIG}-${PLATFORM_CONFIG}/runtime.conf"
BITSTREAM="${SCRIPT_DIR}/../../../sim/generated-src/${PLATFORM}/FireSim-${TARGET_CONFIG}-${PLATFORM_CONFIG}/${PLATFORM}/vivado_proj/firesim.bit"
IMAGE_IMG="${RUN_DIR}/workload.img"
IMAGE_BIN="${RUN_DIR}/workload.bin"

for f in "${DRIVER}" "${BITSTREAM}" "${RUNTIME_CONFIG}" "${IMAGE_IMG}" "${IMAGE_BIN}"; do
	if [ ! -e "${f}" ]; then
		echo "Could not find file ${f}" >&2
		exit 1
	fi
done

PARAMETERS=$(cat "${RUNTIME_CONFIG}" | tr '\n' ' ')
CUSTOM_PARAMETERS=""

if [ ! -z ${CUSTOM_RUNTIME_CONFIG+x} ]; then
	if [ -e "${CUSTOM_RUNTIME_CONFIG}" ]; then
		CUSTOM_PARAMETERS=$(cat "${CUSTOM_RUNTIME_CONFIG}" | tr '\n' ' ')
	elif [ -e "${SCRIPT_DIR}/../${CUSTOM_RUNTIME_CONFIG}" ]; then
		CUSTOM_PARAMETERS=$(cat "${SCRIPT_DIR}/../${CUSTOM_RUNTIME_CONFIG}" | tr '\n' ' ')
	fi
fi

set -x
fpga-util.py -f "${FPGA}" -b "${BITSTREAM}"
${DRIVER} +permissive ${PARAMETERS} ${CUSTOM_PARAMETERS} +slotid="${FPGA}" +blkdev0="${IMAGE_IMG}" +permissive-off "${IMAGE_BIN}"
set +x


exit 0

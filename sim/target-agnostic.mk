# See LICENSE for license details.

# FireSim MAKEFRAG interface - Compulsory variables follow
# The directory into which generated verilog and headers will be dumped
# RTL simulations will also be built here
GENERATED_DIR ?=
# Results from RTL simulations live here
OUTPUT_DIR ?=
# Root name for generated binaries
DESIGN ?=

# SOURCE FILES
# Driver source files
DRIVER_CC ?=
DRIVER_H ?=
# Simulation memory map emitted by the MIDAS compiler
HEADER ?=
# The midas-generated simulator RTL which will be baked into the FPGA shell project
VERILOG ?=

# The target's FIRRTL and associated anotations
FIRRTL_FILE ?=
ANNO_FILE ?=

# The host config package and class string
PLATFORM_CONFIG_PACKAGE ?= firesim.midasexamples
PLATFORM_CONFIG ?= DefaultF1Config

# The name of the generated runtime configuration file
CONF_NAME ?= runtime.conf

# The host platform type
PLATFORM ?= f1

# Target-specific CXX and LD flags
TARGET_CXX_FLAGS ?=
TARGET_LD_FLAGS ?=

simif_dir = $(firesim_base_dir)/midas/src/main/cc
midas_h  = $(shell find $(simif_dir) -name "*.h")
midas_cc = $(shell find $(simif_dir) -name "*.cc") $(shell find $(simif_dir)/vcd-writer -name "*.cpp")

common_cxx_flags := $(TARGET_CXX_FLAGS) -Wno-unused-variable
common_ld_flags := $(TARGET_LD_FLAGS) -lrt

####################################
# Golden Gate Invocation           #
####################################
midas_sbt_project := {file:$(firesim_base_dir)}midas
firesim_root_sbt_project := {file:$(firesim_base_dir)}firesim
# Pre-simulation-mapping annotations which includes all Bridge Annotations
# extracted used to generate new runtime configurations.
fame_annos := $(GENERATED_DIR)/post-bridge-extraction.json

$(VERILOG) $(HEADER) $(fame_annos): $(FIRRTL_FILE) $(ANNO_FILE)
	cd $(base_dir) && $(SBT) "project $(midas_sbt_project)" "runMain midas.stage.GoldenGateMain \
		-o $(VERILOG) -i $(FIRRTL_FILE) -td $(GENERATED_DIR) \
		-faf $(ANNO_FILE) \
		-ggcp $(PLATFORM_CONFIG_PACKAGE) \
		-ggcs $(PLATFORM_CONFIG) \
		-E verilog"
	grep -sh ^ $(GENERATED_DIR)/firrtl_black_box_resource_files.f | \
	xargs cat >> $(VERILOG) # Append blackboxes to FPGA wrapper, if any

####################################
# Runtime-Configuration Generation #
####################################

# This reads in the annotations from a generated target, elaborates a
# FASEDTimingModel if a BridgeAnnoation for one exists, and asks for user input
# to generate a runtime configuration that is compatible with the generated
# hardware (BridgeModule). Useful for modelling a memory system that differs from the default.
.PHONY: conf
conf: $(fame_annos)
	mkdir -p $(GENERATED_DIR)
	cd $(base_dir) && \
	$(SBT) "project $(midas_sbt_project)" "runMain midas.stage.RuntimeConfigGeneratorMain \
		-td $(GENERATED_DIR) \
		-ggaf $(fame_annos) \
		-ggcp $(PLATFORM_CONFIG_PACKAGE) \
		-ggcs $(PLATFORM_CONFIG) \
		-ggrc $(CONF_NAME)"

####################################
# Verilator MIDAS-Level Simulators #
####################################

VERILATOR_CXXOPTS ?= -O0
VERILATOR_MAKEFLAGS ?= -j16 VM_PARALLEL_BUILDS=1

verilator = $(GENERATED_DIR)/V$(DESIGN)
verilator_debug = $(GENERATED_DIR)/V$(DESIGN)-debug

$(verilator) $(verilator_debug): export CXXFLAGS := $(CXXFLAGS) $(common_cxx_flags) $(VERILATOR_CXXOPTS) -D RTLSIM
$(verilator) $(verilator_debug): export LDFLAGS := $(LDFLAGS) $(common_ld_flags)

$(verilator): $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h) $(VERILOG)
	$(MAKE) $(VERILATOR_MAKEFLAGS) -C $(simif_dir) verilator PLATFORM=$(PLATFORM) DESIGN=$(DESIGN) \
	GEN_DIR=$(GENERATED_DIR) DRIVER="$(DRIVER_CC)" TOP_DIR=$(chipyard_dir) VERILATOR_FLAGS="$(EXTRA_VERILATOR_FLAGS)"

$(verilator_debug): $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h) $(VERILOG)
	$(MAKE) $(VERILATOR_MAKEFLAGS) -C $(simif_dir) verilator-debug PLATFORM=$(PLATFORM) DESIGN=$(DESIGN) \
	GEN_DIR=$(GENERATED_DIR) DRIVER="$(DRIVER_CC)" TOP_DIR=$(chipyard_dir) VERILATOR_FLAGS="$(EXTRA_VERILATOR_FLAGS)"

.PHONY: verilator verilator-debug
verilator: $(verilator)
verilator-debug: $(verilator_debug)

##############################
# VCS MIDAS-Level Simulators #
##############################

VCS_CXXOPTS ?= -O2

vcs = $(GENERATED_DIR)/$(DESIGN)
vcs_debug = $(GENERATED_DIR)/$(DESIGN)-debug

$(vcs) $(vcs_debug): export CXXFLAGS := $(CXXFLAGS) $(common_cxx_flags) $(VCS_CXXOPTS) -I$(VCS_HOME)/include -D RTLSIM
$(vcs) $(vcs_debug): export LDFLAGS := $(LDFLAGS) $(common_ld_flags)

$(vcs): $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h) $(VERILOG)
	$(MAKE) -C $(simif_dir) vcs PLATFORM=$(PLATFORM) DESIGN=$(DESIGN) \
	GEN_DIR=$(GENERATED_DIR) DRIVER="$(DRIVER_CC)" TOP_DIR=$(chipyard_dir)

$(vcs_debug): $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h) $(VERILOG)
	$(MAKE) -C $(simif_dir) vcs-debug PLATFORM=$(PLATFORM) DESIGN=$(DESIGN) \
	GEN_DIR=$(GENERATED_DIR) DRIVER="$(DRIVER_CC)" TOP_DIR=$(chipyard_dir)

.PHONY: vcs vcs-debug
vcs: $(vcs)
vcs-debug: $(vcs_debug)

############################
# Master Simulation Driver #
############################
DRIVER_CXXOPTS ?= -O2 -D PLATFORM_$(PLATFORM)

$(OUTPUT_DIR)/$(DESIGN).chain: $(VERILOG)
	mkdir -p $(OUTPUT_DIR)
	$(if $(wildcard $(GENERATED_DIR)/$(DESIGN).chain),cp $(GENERATED_DIR)/$(DESIGN).chain $@,)

$(PLATFORM) = $(OUTPUT_DIR)/$(DESIGN)-$(PLATFORM)
$(PLATFORM): $($(PLATFORM)) $(OUTPUT_DIR)/$(DESIGN).chain

fpga_dir = $(firesim_base_dir)/../platforms/$(PLATFORM)/aws-fpga

$(f1): export CXXFLAGS := $(CXXFLAGS) $(common_cxx_flags) $(DRIVER_CXXOPTS) -I$(fpga_dir)/sdk/userspace/include
# Statically link libfesvr to make it easier to distribute drivers to f1 instances
$(f1): export LDFLAGS := $(LDFLAGS) $(common_ld_flags) -lfpga_mgmt

# Compile Driver
$(f1): $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h) $(runtime_conf)
	mkdir -p $(OUTPUT_DIR)/build
	cp $(HEADER) $(OUTPUT_DIR)/build/
	cp -f $(GENERATED_DIR)/$(CONF_NAME) $(OUTPUT_DIR)/runtime.conf
	$(MAKE) -C $(simif_dir) f1 PLATFORM=f1 DESIGN=$(DESIGN) \
	GEN_DIR=$(OUTPUT_DIR)/build OUT_DIR=$(OUTPUT_DIR) DRIVER="$(DRIVER_CC)" \
	TOP_DIR=$(chipyard_dir)


convey_dir = /opt/convey

convey-app: export CXXFLAGS := $(CXXFLAGS) $(common_cxx_flags) $(DRIVER_CXXOPTS) -I$(fpga_dir)/sdk/userspace/include \
	-I$(convey_dir)/include \
	-I$(convey_dir)/pdk2/latest/wx-2000/include \
	-I$(convey_dir)/driver_build \
	-D__STDC_FORMAT_MACROS
# Statically link libfesvr to make it easier to distribute drivers to f1 instances
convey-app: export LDFLAGS := $(LDFLAGS) $(common_ld_flags) -L$(convey_dir)/lib -l:libwx_runtime.so -L$(convey_dir)/pdk2/latest/wx-2000/lib -lcnyfwd


# Compile Driver
convey-app: $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h) $(runtime_conf)
	mkdir -p $(OUTPUT_DIR)/build
	cp $(HEADER) $(OUTPUT_DIR)/build/
	cp -f $(GENERATED_DIR)/$(CONF_NAME) $(OUTPUT_DIR)/runtime.conf
	$(MAKE) -C $(simif_dir) $(PLATFORM) PLATFORM=$(PLATFORM) DESIGN=$(DESIGN) \
	GEN_DIR=$(OUTPUT_DIR)/build OUT_DIR=$(OUTPUT_DIR) DRIVER="$(DRIVER_CC)" \
	TOP_DIR=$(chipyard_dir)

# Compile Driver
zynq-driver: $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h) $(runtime_conf)
	mkdir -p $(OUTPUT_DIR)/build
	cp $(HEADER) $(OUTPUT_DIR)/build/
	cp -f $(GENERATED_DIR)/$(CONF_NAME) $(OUTPUT_DIR)/runtime.conf
	$(MAKE) -C $(simif_dir) zynq PLATFORM=zynq DESIGN=$(DESIGN) \
	GEN_DIR=$(OUTPUT_DIR)/build OUT_DIR=$(OUTPUT_DIR) DRIVER="$(DRIVER_CC)" \
	TOP_DIR=$(chipyard_dir) CXX=

#########################
# MIDAS Unit Tests      #
#########################
UNITTEST_CONFIG ?= AllUnitTests

rocketchip_dir := $(chipyard_dir)/generators/rocket-chip
unittest_generated_dir := $(base_dir)/generated-src/unittests/$(UNITTEST_CONFIG)
unittest_args = \
		BASE_DIR=$(base_dir) \
		EMUL=$(EMUL) \
		ROCKETCHIP_DIR=$(rocketchip_dir) \
		GEN_DIR=$(unittest_generated_dir) \
		SBT="$(SBT) \"project $(firesim_root_sbt_project)\" " \
		CONFIG=$(UNITTEST_CONFIG) \
		TOP_DIR=$(chipyard_dir)

run-midas-unittests: $(chisel_srcs)
	$(MAKE) -f $(simif_dir)/unittest/Makefrag $@ $(unittest_args)

run-midas-unittests-debug: $(chisel_srcs)
	$(MAKE) -f $(simif_dir)/unittest/Makefrag $@ $(unittest_args)

#########################
# ScalaDoc              #
#########################
scaladoc:
	cd $(base_dir) && $(SBT) "project {file:$(firesim_base_dir)}firesim" "unidoc"

.PHONY: scaladoc
#########################
# Cleaning Recipes      #
#########################
cleanfpga:
	rm -rf $(fpga_work_dir)

mostlyclean:
	rm -rf $(verilator) $(verilator_debug) $(vcs) $(vcs_debug) $($(PLATFORM)) $(OUTPUT_DIR)

clean:
	rm -rf $(GENERATED_DIR) $(OUTPUT_DIR)

veryclean:
	rm -rf generated-src output

tags: $(HEADER) $(DRIVER_CC) $(DRIVER_H) $(midas_cc) $(midas_h)
	ctags -R --exclude=@.ctagsignore .

.PHONY: default verilog compile
.PHONY: $(PLATFORM)-driver
.PHONY: mostlyclean clean

.PRECIOUS: $(OUTPUT_DIR)/%.vpd $(OUTPUT_DIR)/%.out $(OUTPUT_DIR)/%.run

# Remove all implicit suffix rules; This improves make performance substantially as it no longer
# attempts to resolve implicit rules on 1000+ scala files.
.SUFFIXES:

#
# %CopyrightBegin%
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright Ericsson AB 2010-2025. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# %CopyrightEnd%

include $(ERL_TOP)/make/target.mk
include $(ERL_TOP)/make/$(TARGET)/otp.mk

# ----------------------------------------------------
# Application version
# ----------------------------------------------------

include ../vsn.mk

VSN = $(DIAMETER_VSN)

# ----------------------------------------------------
# Release directory specification
# ----------------------------------------------------

RELSYSDIR = $(RELEASE_PATH)/lib/diameter-$(VSN)

# Where to put/find things.
EBIN   = ../ebin
INCDIR = ../include

# Dumbed down to make 3.80. In 3.81 and later it's just $(realpath $(EBIN)).
ABS_EBIN := $(shell cd $(EBIN) && pwd)

# Where make should look for dependencies.
VPATH = .:base:compiler:transport:gen:info

# ----------------------------------------------------
# Target specs
# ----------------------------------------------------

include modules.mk

# Modules generated from dictionary specifications.
DICT_MODULES = $(DICTS:%=gen/diameter_gen_%)
DICT_ERLS    = $(DICT_MODULES:%=%.erl)
DICT_HRLS    = $(DICT_MODULES:%=%.hrl)

# Modules to build before compiling dictionaries.
COMPILER_MODULES = $(notdir $(CT_MODULES)) $(DICT_YRL)

# All handwritten modules from which a depend.mk is generated.
MODULES = \
	$(RT_MODULES) \
	$(CT_MODULES) \
	$(INFO_MODULES)

# Modules whose names are inserted into the app file.
APP_MODULES = \
	$(RT_MODULES) \
	$(DICT_MODULES)

# Modules for which to build beams.
TARGET_MODULES = \
	$(APP_MODULES) \
	$(CT_MODULES) \
	$(INFO_MODULES) \
	$(DICT_YRL:%=gen/%)

# What to build for the 'opt' target.
TARGET_FILES = \
	$(patsubst %, $(EBIN)/%.$(EMULATOR), $(notdir $(TARGET_MODULES))) \
	$(APP_TARGET) \
	$(APPUP_TARGET)

# Subdirectories of src to release modules into.
TARGET_DIRS = $(sort $(dir $(TARGET_MODULES)))

# Ditto for examples.
EXAMPLE_DIRS = $(sort $(dir $(EXAMPLES)))

APP_FILE   = diameter.app
APP_SRC    = $(APP_FILE).src
APP_TARGET = $(EBIN)/$(APP_FILE)

APPUP_FILE   = diameter.appup
APPUP_SRC    = $(APPUP_FILE).src
APPUP_TARGET = $(EBIN)/$(APPUP_FILE)

ifeq ($(TARGET),win32)
  EXE_SUFFIX=.exe
else
  EXE_SUFFIX=
endif

# ----------------------------------------------------
# Flags
# ----------------------------------------------------

ifeq ($(TYPE),debug)
ERL_COMPILE_FLAGS += -Ddebug
endif

# The "warn" flag is on by default, so unless we add the
# the "nowarn" flag, that ("warn") is what we get.
# The "nowarn" flag overrides the "warn" flag if both
# are present!
ifeq ($(NOWARN_MISSING_SPEC_DOCUMENTED),true)
MISSING_SPEC_DOCUMENTED = +nowarn_missing_spec_documented
endif

ERL_COMPILE_FLAGS += \
	+warn_export_vars \
	+warn_unused_vars \
	$(MISSING_SPEC_DOCUMENTED) \
	+'{parse_transform,sys_pre_attributes}' \
	+'{attribute,insert,app_vsn,"$(APP_VSN)"}' \
	-pa $(ABS_EBIN) \
	-I $(INCDIR) \
	-I gen \
	$(STRICT_FLAGS)
# -pa is to be able to include_lib from the include directory: the
# path must contain the application name.

ifeq ($(ERL_DETERMINISTIC),yes)
	DETERMINISM_FLAG = +deterministic
else
	DETERMINISM_FLAG =
endif

# ----------------------------------------------------
# Targets
# ----------------------------------------------------

# erl/hrl from dictionary file.
gen/diameter_gen_%.erl gen/diameter_gen_%.hrl: dict/%.dia
	$(dia_verbose) \
	escript$(EXE_SUFFIX) ../bin/diameterc -o gen -i $(EBIN) $<

opt: $(TARGET_FILES)

# Compile with -Werror during development. Don't do this in the 'opt'
# target so that new warnings don't break the build. It's also
# convenient to have both targets when weeding out warnings isn't the
# priority. (Or when they're intentional, when debugging.)
strict:
	$(MAKE) opt STRICT_FLAGS=-Werror

# Build unofficial patches with some degree of traceability. Refuse to
# build if there are diffs from HEAD since that defeats the purpose.
patch:
	git diff --exit-code HEAD
	$(MAKE) opt PRE_VSN="-$(shell git rev-list --max-count=1 HEAD | cut -c 1-8)"

$(filter-out opt, $(TYPES)):
	@${MAKE} TYPE=$@ opt

# The dictionary parser.
gen/$(DICT_YRL).erl: compiler/$(DICT_YRL).yrl
	$(yecc_verbose) \
	$(ERLC) -Werror $(DETERMINISM_FLAG) -o $(@D) $<

# Generate the app file.
$(APP_TARGET): $(APP_SRC) ../vsn.mk modules.mk
	$(gen_verbose) \
	M=`echo $(notdir $(APP_MODULES)) | tr ' ' ,`; \
	C=`echo $(COMPILER_MODULES) | tr ' ' ,`; \
	I=`echo $(notdir $(INFO_MODULES)) | tr ' ' ,`; \
	R=`echo $(REGISTERED) | tr ' ' ,`; \
	sed -e 's;%VSN%;$(VSN);' \
	    -e "s;%MODULES%;$$M;" \
	    -e "s;%COMPILER%;$$C;" \
	    -e "s;%INFO%;$$I;" \
	    -e "s;%REGISTERED%;$$R;" \
	  $< > $@

$(APPUP_TARGET): $(APPUP_SRC) ../vsn.mk
	$(vsn_verbose) \
	sed -e 's;%VSN%;$(VSN);' $< > $@

app:  $(APP_TARGET) $(APPUP_TARGET)
dict: $(DICT_ERLS)

docs:

list = echo $(1):; echo $($(1)) | tr ' ' '\n' | sort | sed 's@^@  @'

info:
	@echo ========================================
	@$(call list,DICTS)
	@echo
	@$(call list,DICT_YRL)
	@echo
	@$(call list,RT_MODULES)
	@echo
	@$(call list,CT_MODULES)
	@echo
	@$(call list,INFO_MODULES)
	@echo
	@$(call list,TARGET_MODULES)
	@echo
	@$(call list,TARGET_DIRS)
	@echo
	@$(call list,EXTERNAL_HRLS)
	@echo
	@$(call list,INTERNAL_HRLS)
	@echo
	@$(call list,EXAMPLES)
	@echo
	@$(call list,EXAMPLE_DIRS)
	@echo
	@$(call list,BINS)
	@echo ========================================
	@echo "APP_FILE:          $(APP_FILE)"
	@echo "APPUP_FILE:        $(APPUP_FILE)"
	@echo "EXAMPLE_DIRS:      $(EXAMPLE_DIRS)"
	@echo "NOWARN_MISSING_SPEC_DOCUMENTED: $(NOWARN_MISSING_SPEC_DOCUMENTED)"
	@echo "MISSING_SPEC_DOCUMENTED: $(MISSING_SPEC_DOCUMENTED)"
	@echo "ERL_COMPILE_FLAGS: $(ERL_COMPILE_FLAGS)"
	@echo ========================================

clean:
	rm -f $(TARGET_FILES) gen/*
	rm -f $(PLT)
	rm -f depend.mk

realclean: clean
	rm -f ../ebin/*
# Not $(EBIN) just to be a bit paranoid

PLT = ./otp.plt

plt: $(PLT)

$(PLT):
	dialyzer --build_plt \
	         --apps erts stdlib kernel \
	                xmerl ssl public_key crypto \
	                compiler syntax_tools runtime_tools \
	         --output_plt $@ \
	         --get_warnings \
	         --statistics \
	         --verbose

dialyze: opt $(PLT)
	dialyzer --plt $(PLT) \
	         --verbose \
	         $(EBIN)/diameter_gen_base_rfc3588.$(EMULATOR) \
	         $(patsubst %, $(EBIN)/%.$(EMULATOR), \
	                $(notdir $(DICT_YRL) \
	                         $(RT_MODULES) \
	                         $(CT_MODULES) \
	                         $(DICT_MODULES) \
	                         $(INFO_MODULES)))


# ----------------------------------------------------
# Release targets
# ----------------------------------------------------

include $(ERL_TOP)/make/otp_release_targets.mk

# Can't $(INSTALL_DIR) more than one directory at a time on Solaris.

release_spec: opt
	for d in bin ebin examples include src/dict; do \
	    $(INSTALL_DIR) "$(RELSYSDIR)/$$d"; \
	done
	$(INSTALL_SCRIPT) $(BINS:%=../bin/%) "$(RELSYSDIR)/bin"
	$(INSTALL_DATA) $(TARGET_FILES) "$(RELSYSDIR)/ebin"
	$(INSTALL_DATA) $(EXTERNAL_HRLS:%=../include/%) $(DICT_HRLS) \
	                "$(RELSYSDIR)/include"
	$(INSTALL_DATA) $(DICTS:%=dict/%.dia) "$(RELSYSDIR)/src/dict"
	$(MAKE) ERL_DETERMINISTIC=$(ERL_DETERMINISTIC) $(TARGET_DIRS:%/=release_src_%)
	$(MAKE) ERL_DETERMINISTIC=$(ERL_DETERMINISTIC) $(EXAMPLE_DIRS:%/=release_examples_%)

$(TARGET_DIRS:%/=release_src_%): release_src_%:
	$(INSTALL_DIR) "$(RELSYSDIR)/src/$*"
	$(INSTALL_DATA) $(filter $*/%, $(TARGET_MODULES:%=%.erl) \
	                               $(INTERNAL_HRLS)) \
	                $(filter $*/%, compiler/$(DICT_YRL).yrl) \
	                "$(RELSYSDIR)/src/$*"

$(EXAMPLE_DIRS:%/=release_examples_%): release_examples_%:
	$(INSTALL_DIR) "$(RELSYSDIR)/examples/$*"
	$(INSTALL_DATA) $(patsubst %, ../examples/%, $(filter $*/%, $(EXAMPLES))) \
	                "$(RELSYSDIR)/examples/$*"

release_docs_spec:

# ----------------------------------------------------
# Dependencies
# ----------------------------------------------------

gen/diameter_gen_base_accounting.erl gen/diameter_gen_base_accounting.hrl: \
	$(EBIN)/diameter_gen_base_rfc3588.$(EMULATOR)

gen/diameter_gen_acct_rfc6733.erl gen/diameter_gen_acct_rfc6733.hrl: \
	$(EBIN)/diameter_gen_base_rfc6733.$(EMULATOR)

$(DICT_ERLS) $(DICT_HRLS): \
	$(COMPILER_MODULES:%=$(EBIN)/%.$(EMULATOR))

$(DICT_MODULES:gen/%=$(EBIN)/%.$(EMULATOR)): \
	$(INCDIR)/diameter.hrl \
	$(INCDIR)/diameter_gen.hrl

depend: depend.mk

# Generate dependencies makefile.
depend.mk: depend.sed $(MODULES:%=%.erl) Makefile
	(for f in $(MODULES); do \
	     (echo $$f; cat $$f.erl) | sed -f $<; \
	 done) \
	> $@

-include depend.mk

.PHONY: app clean realclean depend dict info release_subdir
.PHONY: debug opt release_docs_spec release_spec
.PHONY: $(TARGET_DIRS:%/=%) $(TARGET_DIRS:%/=release_src_%)
.PHONY: $(EXAMPLE_DIRS:%/=release_examples_%)
.PHONY: plt dialyze patch strict

# Keep intermediate files.
.SECONDARY: $(DICT_ERLS) $(DICT_HRLS) gen/$(DICT_YRL:%=%.erl)

# ----------------------------------------------------
# Targets using secondary expansion (make >= 3.81)
# ----------------------------------------------------

.SECONDEXPANSION:

# Make beams from a subdirectory.
$(TARGET_DIRS:%/=%): \
  $$(patsubst $$@/%, \
              $(EBIN)/%.$(EMULATOR), \
              $$(filter $$@/%, $(TARGET_MODULES) compiler/$(DICT_YRL)))

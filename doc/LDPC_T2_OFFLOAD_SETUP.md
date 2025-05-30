<table style="border-collapse: collapse; border: none;">
  <tr style="border-collapse: collapse; border: none;">
    <td style="border-collapse: collapse; border: none;">
      <a href="http://www.openairinterface.org/">
         <img src="./images/oai_final_logo.png" alt="" border=3 height=50 width=150>
         </img>
      </a>
    </td>
    <td style="border-collapse: collapse; border: none; vertical-align: center;">
      <b><font size = "5">OAI T1/T2 LDPC offload</font></b>
    </td>
  </tr>
</table>

**Table of Contents**

[[_TOC_]]

This documentation aims to provide a tutorial for AMD Xilinx T2 Telco card integration into OAI and its usage.

# Requirements

 - bitstream image and PMD driver for the T2 card provided by AccelerComm
 - DPDK 20.11.9 with patch from Accelercomm: version ACCL_BBDEV_DPDK20.11.3_ldpc_3.1.918.patch
 - tested on RHEL7.9, RHEL9.2, Ubuntu 22.04

# DPDK setup
## DPDK installation
*Note: Following instructions are valid for ACCL_BBDEV_DPDK20.11.3_ldpc_3.1.918.patch version, which is compatible with DPDK 20.11.9. Installation steps, which should be followed for older versions of the patch file (for example ACL_BBDEV_DPDK20.11.3_BL_1006_build_1105_dev_branch_MCT_optimisations_1106_physical_std.patch) are present in older version of this documentation, under the tag 2023.w48.*

```
# Get DPDK source code
git clone https://github.com/DPDK/dpdk-stable.git ~/dpdk-stable
cd ~/dpdk-stable
git checkout v20.11.9
git apply ~/ACL_BBDEV_DPDK20.11.3_ldpc_3.1.918.patch
```
Replace `~/ACL_BBDEV_DPDK20.11.3_ldpc_3.1.918.patch` by patch file provided by
Accelercomm.
```
cd ~/dpdk-stable
meson setup build
# meson setup --prefix=/opt/dpdk-t2 build for installation with non-default installation prefix
cd build
ninja
sudo ninja install
sudo ldconfig
```
## DPDK configuration
 - load required kernel module
```
sudo modprobe vfio-pci
```
 - check presence of the card and its PCI addres on the host machine
```
lspci | grep "Xilinx"
```
 - bind the card with vfio-pci driver
```
sudo python3 ~/dpdk-stable/usertools/dpdk-devbind.py --bind=vfio-pci 41:00.0
```
Replace PCI address of the card *41:00.0* by address detected by *lspci | grep "Xilinx"* command
 - hugepages setup (10 x 1GB hugepages)
```
sudo python3 ~/dpdk-stable/usertools/dpdk-hugepages.py -p 1G --setup 10G
```

*Note: device binding and hugepages setup has to be done after every reboot of
the host machine*

# Modifications in the OAI code
## DPDK lib and PMD path specification
If DPDK library was installed into custom path, you have to point to the right directory with `PKG_CONFIG_PATH`, prior to the OAI build. Sample command to set the DPDK path to `/opt/dpdk-t2/lib64/pkgconfig/`:
```
export PKG_CONFIG_PATH=/opt/dpdk-t2/lib64/pkgconfig/:$PKG_CONFIG_PATH
```

# OAI Build
OTA deployment is precisely described in the following tutorial:
- [NR_SA_Tutorial_COTS_UE](https://gitlab.eurecom.fr/oai/openairinterface5g/-/blob/develop/doc/NR_SA_Tutorial_COTS_UE.md)
Instead of section *3.2 Build OAI gNB* from the tutorial, run following commands:

```
# Get openairinterface5g source code
git clone https://gitlab.eurecom.fr/oai/openairinterface5g.git ~/openairinterface5g
cd ~/openairinterface5g
git checkout develop

# Install OAI dependencies
cd ~/openairinterface5g/cmake_targets
./build_oai -I

# Build OAI gNB
cd ~/openairinterface5g
source oaienv
cd cmake_targets
./build_oai -w USRP --ninja --gNB -P --build-lib "ldpc_t2" -C
```

Shared object file *libldpc_t2.so* is created during the compilation. This object is conditionally compiled. Selection of the library to compile is done using *--build-lib ldpc_t2*.

*Required poll mode driver has to be present on the host machine and required DPDK version has to be installed on the host, prior to the build of OAI*

# Setup of T2-related DPDK EAL parameters
To configure T2-related DPDK Environment Abstraction Layer (EAL) parameters, you can set the following parameters via the command line of PHY simulators or softmodem:
- `nrLDPC_coding_t2.dpdk_dev` - **mandatory** parameter, specifies PCI address of the T2 card. PCI address of the T2 card can be detected by `lspci | grep "Xilinx"` command.
- `nrLDPC_coding_t2.dpdk_core_list` - **mandatory** parameter, specifies CPU cores assigned to DPDK for T2 processing. Ensure that the CPU cores specified in *nrLDPC_coding_t2.dpdk_core_list* are available and not used by other processes to avoid conflicts.
- `nrLDPC_coding_t2.dpdk_prefix` - DPDK shared data file prefix, by default set to *b6*.

**Note:** These parameters can also be provided in a configuration file:
```
nrLDPC_coding_t2 : {
  dpdk_dev : "41:00.0";
  dpdk_core_list : "14-15";
};

loader : {
  ldpc : {
    shlibversion : "_t2";
  };
};
```

# 5G PHY simulators

## nr_ulsim test
Offload of the channel decoding to the T2 card is in nr_ulsim specified by *--loader.ldpc.shlibversion _t2* option. Example command for running nr_ulsim with LDPC decoding offload to the T2 card:
```
cd ~/openairinterface5g
source oaienv
cd cmake_targets/ran_build/build
sudo ./nr_ulsim -n100 -s20 -m20 -r273 -R273 --loader.ldpc.shlibversion _t2 --nrLDPC_coding_t2.dpdk_dev 01:00.0 --nrLDPC_coding_t2.dpdk_core_list 0-1
```
## nr_dlsim test
Offload of the channel encoding to the AMD Xilinx T2 card is in nr_dlsim specified by *-c* option. Example command for running nr_dlsim with LDPC encoding offload to the T2 card:
```
cd ~/openairinterface5g
source oaienv
cd cmake_targets/ran_build/build
sudo ./nr_dlsim -n300 -s30 -R 106 -e 27 --loader.ldpc.shlibversion _t2 --nrLDPC_coding_t2.dpdk_dev 01:00.0 --nrLDPC_coding_t2.dpdk_core_list 0-1
```

# OTA test
Offload of the channel encoding and decoding to the AMD Xilinx T2 card is enabled by *--loader.ldpc.shlibversion _t2* option.

## Run OAI gNB with USRP B210
```
cd ~/openairinterface5g
source oaienv
cd cmake_targets/ran_build/build
sudo ./nr-softmodem -O ../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf --loader.ldpc.shlibversion _t2 --nrLDPC_coding_t2.dpdk_dev 01:00.0 --nrLDPC_coding_t2.dpdk_core_list 0-1
```

# Limitations
## AMD Xilinx T2 card
 - functionality of the LDPC encoding and decoding offload verified in OTA SISO setup with USRP N310 and Quectel RM500Q, blocking of the card reported for MIMO setup (2 layers)

*Note: AMD Xilinx T1 Telco card is not supported anymore.*

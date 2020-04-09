# Necessary files for Elastic-Hurryup
sudo apt-get update
sudo apt-get install -y linux-tools-$(uname -r) #cpupower
sudo apt-get install acpi-cpufreq #acpi

# Force acpi
cd /etc/default
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="/GRUB_CMDLINE_LINUX_DEFAULT="intel_pstate=nowhwp intel_pstate=disable acpi=force/g' grub

sudo update-grub
# sudo reboot

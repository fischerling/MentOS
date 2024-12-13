#!/bin/sh

ROOT=$1

if [ -z "${ROOT}" ]
then
	echo "Usage: $0 <root>"
	exit 1
fi

# Set default permissions and file owner
chown root:root -R "${ROOT}"
chmod -R u=rwX,go=rX -R "${ROOT}"

chmod 777 "${ROOT}/tmp"

chmod -R a+rx "${ROOT}"/bin

# Set security related permissions
chmod 644 "${ROOT}"/etc/passwd
chmod og-rwx "${ROOT}"/etc/shadow

# Set SUID on doas
sudo chmod u+s "${ROOT}"/bin/doas
chmod u+s "${ROOT}"/usr/bin/exercises/*/setup
chmod u+s "${ROOT}"/usr/bin/exercises/*/checkup

# Set user permissions
chown 1000:1000 -R "${ROOT}"/home/alice
chown 1001:1001 -R "${ROOT}"/home/bob
chmod -R u=rwX,go= -R "${ROOT}"/home/*

# Make intro dir writable
chown 1000:1000 "${ROOT}"/var/lib/intro/1000

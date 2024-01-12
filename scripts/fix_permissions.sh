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

# Set security related permissions
chmod 644 "${ROOT}"/etc/passwd
chmod og-rwx "${ROOT}"/etc/shadow

# Set suid on doas
sudo chmod +s "${ROOT}"/bin/doas

# Set user permissions
chown 1000:1000 -R "${ROOT}"/home/alice
chown 1001:1001 -R "${ROOT}"/home/bob
chmod -R u=rwX,go= -R "${ROOT}"/home/*

import uuid_utils
import uuid_utils.compat

import c_uuid_v7

print("c_uuid_v7:")
for _ in range(10):
    print(c_uuid_v7.uuid7())

print("\nc_uuid_v7.compat:")
for _ in range(10):
    print(c_uuid_v7.compat.uuid7())

print("\nuuid_utils:")
for _ in range(10):
    print(uuid_utils.uuid7())

print("\nuuid_utils.compat:")
for _ in range(10):
    print(uuid_utils.compat.uuid7())
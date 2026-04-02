import uuid_utils
import c_uuid_v7

print("uuid_utils:")
for _ in range(30):
    print(uuid_utils.uuid7())

print("\nc_uuid_v7:")
for _ in range(30):
    print(c_uuid_v7.uuid7())
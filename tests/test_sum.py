import c_uuid_v7


def test_sum() -> None:
    assert c_uuid_v7.sum(2, 3) == 5

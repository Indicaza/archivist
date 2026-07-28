from models import User


def describe_user(user: User) -> str:
    state = "active" if user.active else "inactive"
    return f"{user.display_name} ({state})"


def sample_user() -> User:
    return User(user_id="user-1", display_name="Mnemosyne")

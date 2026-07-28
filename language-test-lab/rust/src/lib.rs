#[derive(Debug, Clone, PartialEq, Eq)]
pub struct User {
    pub display_name: String,
}

pub fn greeting(user: &User) -> String {
    format!("Hello, {}!", user.display_name)
}

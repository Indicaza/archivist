use archivist_language_test::{greeting, User};

fn main() {
    let user = User {
        display_name: String::from("Mnemosyne"),
    };

    println!("{}", greeting(&user));
}

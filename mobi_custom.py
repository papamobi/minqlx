import minqlx


class mobi_custom(minqlx.Plugin):
    def __init__(self):
        self.add_hook("player_items_toss", self.handle_player_items_toss)

    def handle_player_items_toss(self, player):
        player.weapon(1)  # disable weapon droping

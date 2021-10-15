import minqlx


class mobi_custom(minqlx.Plugin):
    def __init__(self):
        self.add_hook("player_items_toss", self.handle_player_items_toss)
        self.add_hook("round_start", self.handle_round_start)

    def handle_player_items_toss(self, player):
        player.weapon(1)  # disable weapon droping

    @minqlx.delay(0.2)
    def handle_round_start(self, *args, **kwargs):
        minqlx.fix_takedamage()

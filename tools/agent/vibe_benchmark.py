#!/usr/bin/env python3
"""Prepare genuine prompt-to-map trials and aggregate completed visual reviews.

This does not fabricate maps, reviews or aesthetic scores. The visual agent/human
runs each prompt through Ariane and supplies provenance-bound review.json files.
"""
import argparse
import json
from pathlib import Path
import statistics

CASES = [
    ('desert-market', 'Un petit marché clandestin dans une station-service abandonnée.', 'Garde les bâtiments, agrandis seulement les stands.'),
    ('motel-terrace', 'Une terrasse de motel dans le désert, accueillante mais usée.', 'Moins rangé, sans bloquer les passages.'),
    ('military-checkpoint', 'Un checkpoint militaire improvisé avec passage pour les voitures.', 'Élargis le passage en gardant la guérite.'),
    ('urban-alley', 'Une ruelle de Los Santos dense et vivante.', 'Ajoute des détails seulement contre les murs.'),
    ('beach-camp', 'Un petit campement de plage chaleureux.', 'Rends-le plus abandonné en gardant le foyer.'),
    ('farm-yard', 'Une cour de ferme crédible avec outils et stockage.', 'Déplace le stockage, conserve les accès.'),
    ('industrial-yard', 'Une zone industrielle désaffectée avec une voie de camion.', 'Plus de désordre mais garde la voie libre.'),
    ('park-rest', 'Un coin tranquille dans un parc, bancs et végétation.', 'Garde ces arbres et rapproche les bancs.'),
    ('roadside-diner', 'Une pause routière rétro autour de ce bâtiment.', 'Propose une variante plus modeste.'),
    ('street-food', 'Un coin street food populaire avec plusieurs vendeurs.', 'Agrandis le marché sans déplacer son entrée.'),
    ('scrap-yard', 'Une petite casse qui raconte une histoire.', 'Réduis la densité autour du portail.'),
    ('forest-outpost', 'Un poste isolé en forêt, discret et utilitaire.', 'Rends-le plus habité sans ajouter de bâtiment.'),
    ('suburban-garden', 'Un jardin de quartier avec coin repas.', 'Annule seulement la dernière modification aux sièges.'),
    ('dock-storage', 'Un stockage portuaire usé avec circulation claire.', 'Tourne tout le groupe sans casser les piles.'),
    ('festival', 'Un petit événement extérieur avec une scène principale.', 'Garde la scène, refais le coin public.'),
    ('abandoned-playground', 'Une aire de repos oubliée depuis longtemps.', 'Moins de déchets, plus de végétation.'),
    ('garage-yard', 'La cour vivante d’un petit garage.', 'Libère complètement la porte du garage.'),
    ('western-street', 'Une petite rue western dans le désert.', 'Conserve la silhouette, ajoute des détails à hauteur de joueur.'),
    ('hill-camp', 'Un campement sur ce terrain en pente.', 'Corrige les objets flottants sans aplatir toute la composition.'),
    ('night-meeting', 'Un lieu de rendez-vous discret la nuit.', 'Compare avec une ambiance de fin d’après-midi.'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['init', 'report'])
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    if args.command == 'init':
        args.directory.mkdir(parents=True, exist_ok=True)
        for name, brief, followup in CASES:
            directory = args.directory / name
            directory.mkdir(exist_ok=True)
            path = directory / 'brief.json'
            if not path.exists():
                path.write_text(json.dumps({'id':name,'brief':brief,'followup':followup,
                    'required_artifacts':['initial/review.json','retouch/review.json'],
                    'human_preference':None,'seconds_to_acceptable':None,'correction_count':None},
                    ensure_ascii=False,indent=2))
        print(json.dumps({'prepared':len(CASES),'directory':str(args.directory)}))
        return
    reports = []
    for name, _, _ in CASES:
        stages = {}
        for stage in ('initial','retouch'):
            path = args.directory / name / stage / 'review.json'
            if not path.exists():
                stages[stage] = {'status':'missing'}
                continue
            review = json.loads(path.read_text())
            visual = review.get('visual_review',{})
            if visual.get('status') != 'reviewed':
                stages[stage] = {'status':'unreviewed'}
                continue
            stages[stage] = {'status':'reviewed','scores':visual['scores'],
                             'accepted':visual.get('accepted',False),
                             'geometry_valid':review['validation']['valid'],
                             'mean_score':statistics.mean(visual['scores'].values())}
        reports.append({'case':name,'stages':stages})
    completed = [r for r in reports if all(s['status']=='reviewed' for s in r['stages'].values())]
    result = {'completed_cases':len(completed),'total_cases':len(CASES),'cases':reports,
              'note':'Scores are agent/human judgments, not objective aesthetic measurements.'}
    (args.directory / 'report.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__ == '__main__': main()
